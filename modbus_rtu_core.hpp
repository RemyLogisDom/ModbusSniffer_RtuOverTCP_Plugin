// ============================================================================
// Moteur de décodage Modbus RTU -- header-only, sans dépendance à Qt.
// Inclus par ModbusRtuMoniteur (wrapper Qt) mais utilisable seul.
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>
#include <deque>
#include <string>
#include <map>
#include <set>
#include <optional>
#include <chrono>

namespace modbus_rtu {

using Octets = std::vector<uint8_t>;

// ---------------------------------------------------------------------------
// CRC16 Modbus
// ---------------------------------------------------------------------------

inline uint16_t crc16_modbus(const Octets& data) {
    uint16_t crc = 0xFFFF;
    for (uint8_t b : data) {
        crc ^= b;
        for (int i = 0; i < 8; ++i) {
            crc = (crc & 0x0001) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
        }
    }
    return crc;
}

inline bool verifier_crc(const Octets& trame) {
    if (trame.size() < 4) return false;
    Octets corps(trame.begin(), trame.end() - 2);
    uint16_t crc_calc = crc16_modbus(corps);
    uint16_t crc_recu = static_cast<uint16_t>(trame[trame.size() - 2]) |
                         (static_cast<uint16_t>(trame[trame.size() - 1]) << 8);
    return crc_calc == crc_recu;
}

// ---------------------------------------------------------------------------
// Codes fonction
// ---------------------------------------------------------------------------

enum FonctionModbus : uint8_t {
    LIRE_BOBINES               = 0x01,
    LIRE_ENTREES_TOR           = 0x02,
    LIRE_REGISTRES_HOLDING     = 0x03,
    LIRE_REGISTRES_INPUT       = 0x04,
    ECRIRE_BOBINE_UNIQUE       = 0x05,
    ECRIRE_REGISTRE_UNIQUE     = 0x06,
    ECRIRE_BOBINES_MULTIPLES   = 0x0F,
    ECRIRE_REGISTRES_MULTIPLES = 0x10,
};

// ---------------------------------------------------------------------------
// Resynchronisation par CRC (flux SANS timing fiable -- passerelle
// Modbus RTU/Ethernet). Alimenter au fil de l'eau via alimenter().
// ---------------------------------------------------------------------------

// Trame brute découpée dans le flux, avec son sens tel que déduit par
// l'extracteur (requête du maître ou réponse de l'esclave).
struct TrameBrute {
    Octets octets;
    bool est_reponse = false;
};

class ExtracteurTramesSansTemporisation {
public:
    explicit ExtracteurTramesSansTemporisation(
            std::optional<std::set<uint8_t>> adresses_esclaves_connues = std::nullopt)
        : adresses_connues_(std::move(adresses_esclaves_connues)) {}

    std::vector<TrameBrute> alimenter(const Octets& nouveaux_octets) {
        buffer_.insert(buffer_.end(), nouveaux_octets.begin(), nouveaux_octets.end());
        std::vector<TrameBrute> trames;
        while (auto trame = extraire_une_trame()) {
            trames.push_back(std::move(*trame));
        }
        return trames;
    }

    std::uint64_t trames_rejetees = 0;

private:
    struct Attente { uint8_t esclave; uint8_t fonction; };

    static constexpr int INVALIDE = -1;   // en-tête incompatible à cette position
    static constexpr int INCOMPLET = -2;  // pas encore assez d'octets pour conclure

    std::deque<uint8_t> buffer_;
    std::optional<Attente> attente_;
    std::optional<std::set<uint8_t>> adresses_connues_;

    // Longueur totale (CRC compris) si buffer_ commence par une requête maître.
    int longueur_requete(uint8_t fonction) const {
        switch (fonction) {
            case LIRE_BOBINES: case LIRE_ENTREES_TOR:
            case LIRE_REGISTRES_HOLDING: case LIRE_REGISTRES_INPUT:
            case ECRIRE_BOBINE_UNIQUE: case ECRIRE_REGISTRE_UNIQUE:
                return 8;
            case ECRIRE_BOBINES_MULTIPLES:
            case ECRIRE_REGISTRES_MULTIPLES: {
                // esclave, fonction, départ(2), quantité(2), nb_octets, données, CRC(2)
                if (buffer_.size() < 7) return INCOMPLET;
                return 7 + buffer_[6] + 2;
            }
            default:
                return INVALIDE;
        }
    }

    // Longueur totale (CRC compris) si buffer_ commence par la réponse à la
    // requête en attente : même esclave et même fonction (ou son exception).
    int longueur_reponse(uint8_t adresse, uint8_t fonction_brute) const {
        if (adresse != attente_->esclave) return INVALIDE;
        if (fonction_brute == (attente_->fonction | 0x80)) return 5;
        if (fonction_brute != attente_->fonction) return INVALIDE;
        switch (fonction_brute) {
            case ECRIRE_BOBINE_UNIQUE: case ECRIRE_REGISTRE_UNIQUE:
            case ECRIRE_BOBINES_MULTIPLES: case ECRIRE_REGISTRES_MULTIPLES:
                return 8;
            case LIRE_BOBINES: case LIRE_ENTREES_TOR:
            case LIRE_REGISTRES_HOLDING: case LIRE_REGISTRES_INPUT:
                // esclave, fonction, nb_octets, données, CRC(2)
                return 3 + buffer_[2] + 2;
            default:
                return INVALIDE;
        }
    }

    std::optional<TrameBrute> extraire_une_trame() {
        while (buffer_.size() >= 4) {
            uint8_t adresse = buffer_[0];
            uint8_t fonction_brute = buffer_[1];

            if (adresses_connues_.has_value() && adresses_connues_->count(adresse) == 0) {
                trames_rejetees++;
                buffer_.pop_front();
                continue;
            }

            // Deux lectures possibles à cette position : la réponse attendue,
            // ou une nouvelle requête (réponse perdue, esclave muet, broadcast).
            bool incomplet = false;
            if (attente_.has_value()) {
                if (auto trame = essayer(longueur_reponse(adresse, fonction_brute), true, incomplet))
                    return trame;
            }
            if (auto trame = essayer(longueur_requete(fonction_brute), false, incomplet))
                return trame;

            if (incomplet) return std::nullopt;  // attendre la suite du flux
            trames_rejetees++;
            buffer_.pop_front();
        }
        return std::nullopt;
    }

    std::optional<TrameBrute> essayer(int longueur, bool est_reponse, bool& incomplet) {
        if (longueur == INVALIDE) return std::nullopt;
        if (longueur == INCOMPLET || buffer_.size() < static_cast<std::size_t>(longueur)) {
            incomplet = true;
            return std::nullopt;
        }
        Octets candidate(buffer_.begin(), buffer_.begin() + longueur);
        if (!verifier_crc(candidate)) return std::nullopt;

        buffer_.erase(buffer_.begin(), buffer_.begin() + longueur);
        if (est_reponse) attente_.reset();
        else attente_ = Attente{candidate[0], candidate[1]};
        return TrameBrute{std::move(candidate), est_reponse};
    }
};

// ---------------------------------------------------------------------------
// Décodage du contenu des trames
// ---------------------------------------------------------------------------

struct ContenuTrame {
    std::optional<uint16_t> adresse_depart;
    std::optional<uint16_t> quantite;
    std::optional<uint16_t> adresse;
    std::optional<uint16_t> valeur;
    std::vector<uint16_t> valeurs;
    std::optional<uint8_t> code_exception;
};

inline uint16_t lire_u16(const Octets& o, size_t pos) {
    return (static_cast<uint16_t>(o[pos]) << 8) | o[pos + 1];
}

// Toutes les lectures sont bornées par corps.size() : une trame qui passe le
// CRC par hasard (resynchronisation sans timing) ou mal appairée ne doit
// jamais faire lire hors du tampon. Une trame incohérente donne un contenu vide.

inline ContenuTrame decoder_requete(uint8_t fonction, const Octets& corps) {
    ContenuTrame c;
    switch (fonction) {
        case LIRE_BOBINES: case LIRE_ENTREES_TOR:
        case LIRE_REGISTRES_HOLDING: case LIRE_REGISTRES_INPUT:
            if (corps.size() < 4) break;
            c.adresse_depart = lire_u16(corps, 0);
            c.quantite = lire_u16(corps, 2);
            break;
        case ECRIRE_BOBINE_UNIQUE: {
            if (corps.size() < 4) break;
            uint16_t valeur_brute = lire_u16(corps, 2);
            c.adresse = lire_u16(corps, 0);
            c.valeur = (valeur_brute == 0xFF00) ? 1 : 0;
            break;
        }
        case ECRIRE_REGISTRE_UNIQUE:
            if (corps.size() < 4) break;
            c.adresse = lire_u16(corps, 0);
            c.valeur = lire_u16(corps, 2);
            break;
        case ECRIRE_BOBINES_MULTIPLES: {
            // départ(2), quantité(2), nb_octets, données
            if (corps.size() < 5) break;
            uint16_t quantite = lire_u16(corps, 2);
            uint8_t nb_octets = corps[4];
            if (corps.size() < 5u + nb_octets || quantite > nb_octets * 8) break;
            c.adresse_depart = lire_u16(corps, 0);
            c.quantite = quantite;
            for (int i = 0; i < quantite; ++i) {
                uint8_t octet = corps[5 + i / 8];
                c.valeurs.push_back((octet >> (i % 8)) & 1);
            }
            break;
        }
        case ECRIRE_REGISTRES_MULTIPLES: {
            if (corps.size() < 5) break;
            uint16_t quantite = lire_u16(corps, 2);
            uint8_t nb_octets = corps[4];
            if (corps.size() < 5u + nb_octets || quantite * 2 > nb_octets) break;
            c.adresse_depart = lire_u16(corps, 0);
            c.quantite = quantite;
            for (int i = 0; i < quantite; ++i) c.valeurs.push_back(lire_u16(corps, 5 + 2 * i));
            break;
        }
        default:
            break;
    }
    return c;
}

inline ContenuTrame decoder_reponse(uint8_t fonction, const Octets& corps,
                                     const std::optional<ContenuTrame>& requete_associee) {
    ContenuTrame c;
    switch (fonction) {
        case LIRE_REGISTRES_HOLDING: case LIRE_REGISTRES_INPUT: {
            // nb_octets, données
            if (corps.empty()) break;
            uint8_t nb_octets = corps[0];
            if (corps.size() < 1u + nb_octets) break;
            for (int i = 0; i < nb_octets / 2; ++i) c.valeurs.push_back(lire_u16(corps, 1 + 2 * i));
            // Sans requête cohérente on ne sait pas où ranger les valeurs.
            if (requete_associee && requete_associee->quantite &&
                    *requete_associee->quantite * 2 == nb_octets)
                c.adresse_depart = requete_associee->adresse_depart;
            break;
        }
        case LIRE_BOBINES: case LIRE_ENTREES_TOR: {
            if (corps.empty()) break;
            uint8_t nb_octets = corps[0];
            if (corps.size() < 1u + nb_octets) break;
            const bool requete_coherente = requete_associee && requete_associee->quantite &&
                    (*requete_associee->quantite + 7) / 8 == nb_octets;
            uint16_t quantite = requete_coherente ? *requete_associee->quantite
                                                  : static_cast<uint16_t>(nb_octets * 8);
            for (int i = 0; i < quantite; ++i) {
                uint8_t octet = corps[1 + i / 8];
                c.valeurs.push_back((octet >> (i % 8)) & 1);
            }
            if (requete_coherente) c.adresse_depart = requete_associee->adresse_depart;
            break;
        }
        case ECRIRE_BOBINE_UNIQUE: case ECRIRE_REGISTRE_UNIQUE: {
            if (corps.size() < 4) break;
            uint16_t valeur = lire_u16(corps, 2);
            if (fonction == ECRIRE_BOBINE_UNIQUE) valeur = (valeur == 0xFF00) ? 1 : 0;
            c.adresse = lire_u16(corps, 0);
            c.valeur = valeur;
            break;
        }
        case ECRIRE_BOBINES_MULTIPLES: case ECRIRE_REGISTRES_MULTIPLES:
            if (corps.size() < 4) break;
            c.adresse_depart = lire_u16(corps, 0);
            c.quantite = lire_u16(corps, 2);
            break;
        default:
            break;
    }
    return c;
}

struct TrameDecodee {
    double timestamp = 0.0;
    Octets brute;
    uint8_t adresse_esclave = 0;
    uint8_t code_fonction = 0;
    bool est_exception = false;
    std::string type_trame;
    ContenuTrame contenu;
    bool crc_valide = true;
};

inline TrameDecodee decoder_trame(double timestamp, const Octets& trame_brute, bool est_reponse,
                                   const std::optional<ContenuTrame>& requete_associee) {
    TrameDecodee t;
    t.timestamp = timestamp;
    t.brute = trame_brute;
    t.crc_valide = verifier_crc(trame_brute);
    if (trame_brute.size() < 4) {  // esclave + fonction + CRC au minimum
        t.crc_valide = false;
        t.type_trame = "crc_invalide";
        return t;
    }
    t.adresse_esclave = trame_brute[0];
    uint8_t code_fonction_brut = trame_brute[1];
    t.est_exception = (code_fonction_brut & 0x80) != 0;
    t.code_fonction = code_fonction_brut & 0x7F;
    Octets corps(trame_brute.begin() + 2, trame_brute.end() - 2);

    if (t.est_exception) {
        t.type_trame = "reponse";
        if (!corps.empty()) t.contenu.code_exception = corps[0];
        return t;
    }
    if (!est_reponse) {
        t.contenu = decoder_requete(t.code_fonction, corps);
        t.type_trame = "requete";
    } else {
        t.contenu = decoder_reponse(t.code_fonction, corps, requete_associee);
        t.type_trame = "reponse";
    }
    return t;
}

// ---------------------------------------------------------------------------
// Appairage requête/réponse + suivi des registres dans le temps
// ---------------------------------------------------------------------------

struct EvenementRegistre {
    double timestamp;
    uint8_t esclave;
    std::string type_zone;
    uint16_t adresse;
    uint16_t valeur;
    std::string source;
};

class SuiviModbus {
public:
    explicit SuiviModbus(std::optional<std::set<uint8_t>> adresses_esclaves_connues = std::nullopt)
        : extracteur_(std::move(adresses_esclaves_connues)) {}

    std::vector<TrameDecodee> alimenter_flux(const Octets& octets, double timestamp) {
        auto trames_brutes = extracteur_.alimenter(octets);
        std::vector<TrameDecodee> resultats;
        resultats.reserve(trames_brutes.size());
        for (const auto& tb : trames_brutes) resultats.push_back(traiter_trame_brute(timestamp, tb));
        return resultats;
    }

    TrameDecodee traiter_trame_brute(double timestamp, const TrameBrute& trame_brute) {
        const Octets& octets = trame_brute.octets;
        if (!verifier_crc(octets)) {
            TrameDecodee vide;
            vide.crc_valide = false;
            vide.type_trame = "crc_invalide";
            return vide;
        }

        // Le sens (requête/réponse) vient de l'extracteur, qui a déjà vérifié
        // qu'une réponse suit une requête du même esclave et de même fonction.
        // On revérifie ici avant de reprendre le contenu de la requête.
        std::optional<ContenuTrame> requete_associee;
        if (trame_brute.est_reponse) {
            if (requete_en_attente_.has_value() && requete_en_attente_->esclave == octets[0] &&
                    requete_en_attente_->fonction == (octets[1] & 0x7F))
                requete_associee = requete_en_attente_->contenu;
            requete_en_attente_.reset();
        }

        TrameDecodee trame = decoder_trame(timestamp, octets, trame_brute.est_reponse, requete_associee);
        if (!trame_brute.est_reponse)
            requete_en_attente_ = RequeteEnAttente{trame.adresse_esclave, trame.code_fonction,
                                                    trame.contenu, timestamp};
        appliquer_evenements(trame);
        return trame;
    }

    const std::map<uint8_t, std::map<std::string, std::map<uint16_t, uint16_t>>>&
    etat_courant() const { return registres_; }

    // Rend les mises à jour de registres produites depuis l'appel précédent
    // et les retire du moteur : rien n'est conservé, la mémoire reste bornée.
    std::vector<EvenementRegistre> extraire_evenements() {
        std::vector<EvenementRegistre> evenements;
        evenements.swap(evenements_);
        return evenements;
    }

    std::uint64_t trames_rejetees() const { return extracteur_.trames_rejetees; }

private:
    struct RequeteEnAttente { uint8_t esclave; uint8_t fonction; ContenuTrame contenu; double timestamp; };

    std::map<uint8_t, std::map<std::string, std::map<uint16_t, uint16_t>>> registres_;
    std::vector<EvenementRegistre> evenements_;
    std::optional<RequeteEnAttente> requete_en_attente_;
    ExtracteurTramesSansTemporisation extracteur_;

    void maj_registre(double ts, uint8_t esclave, const std::string& zone,
                       uint16_t adresse, uint16_t valeur, const std::string& source) {
        registres_[esclave][zone][adresse] = valeur;
        evenements_.push_back({ts, esclave, zone, adresse, valeur, source});
    }

    static std::optional<std::string> zone_par_fonction(uint8_t fonction) {
        switch (fonction) {
            case LIRE_BOBINES: case ECRIRE_BOBINE_UNIQUE: case ECRIRE_BOBINES_MULTIPLES:
                return "coil";
            case LIRE_ENTREES_TOR:
                return "discrete_input";
            case LIRE_REGISTRES_HOLDING: case ECRIRE_REGISTRE_UNIQUE: case ECRIRE_REGISTRES_MULTIPLES:
                return "holding";
            case LIRE_REGISTRES_INPUT:
                return "input";
            default:
                return std::nullopt;
        }
    }

    void appliquer_evenements(const TrameDecodee& trame) {
        if (trame.est_exception) return;
        auto zone_opt = zone_par_fonction(trame.code_fonction);
        if (!zone_opt) return;
        const std::string& zone = *zone_opt;

        bool est_lecture = (trame.code_fonction == LIRE_BOBINES || trame.code_fonction == LIRE_ENTREES_TOR ||
                             trame.code_fonction == LIRE_REGISTRES_HOLDING || trame.code_fonction == LIRE_REGISTRES_INPUT);

        if (trame.type_trame == "reponse" && est_lecture) {
            if (trame.contenu.adresse_depart) {
                uint16_t depart = *trame.contenu.adresse_depart;
                for (size_t i = 0; i < trame.contenu.valeurs.size(); ++i) {
                    maj_registre(trame.timestamp, trame.adresse_esclave, zone,
                                 static_cast<uint16_t>(depart + i), trame.contenu.valeurs[i], "lecture");
                }
            }
        } else if (trame.code_fonction == ECRIRE_BOBINE_UNIQUE || trame.code_fonction == ECRIRE_REGISTRE_UNIQUE) {
            if (trame.contenu.adresse && trame.contenu.valeur) {
                maj_registre(trame.timestamp, trame.adresse_esclave, zone,
                             *trame.contenu.adresse, *trame.contenu.valeur, "ecriture");
            }
        } else if (trame.code_fonction == ECRIRE_BOBINES_MULTIPLES || trame.code_fonction == ECRIRE_REGISTRES_MULTIPLES) {
            if (trame.type_trame == "requete" && trame.contenu.adresse_depart) {
                uint16_t depart = *trame.contenu.adresse_depart;
                for (size_t i = 0; i < trame.contenu.valeurs.size(); ++i) {
                    maj_registre(trame.timestamp, trame.adresse_esclave, zone,
                                 static_cast<uint16_t>(depart + i), trame.contenu.valeurs[i], "ecriture");
                }
            }
        }
    }
};

} // namespace modbus_rtu
