// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

let autoform_default = (function() {
    let self = this;

    this.key = 'tuto';

    this.pages = [
        {
            key: 'tuto',
            title: 'Tutoriel',
            script: `// Retirer le commentaire de la ligne suivante pour afficher les
// champs (texte, numérique, etc.) à droite du libellé.
// form.pushOptions({large: true});

form.output(html\`
    <p>Une <b>fonction</b> est composée d'un <i>nom</i> et de plusieurs <i>paramètres</i> et permet de proposer un outil de saisie (champ texte, menu déroulant ...).
    <p>Exemple : la fonction form.text("num_patient", "Numéro de patient") propose un champ de saisie texte intitulé <i>Numéro de patient</i> et le stocke dans la variable <i>num_patient</i>.
    <p>Vous pouvez copier les fonctions présentées dans la section <b>Exemples</b> dans <b>Nouvelle section</b> pour créer votre propre formulaire.
\`);

form.section("Nouvelle section", () => {
    // Copier coller les fonctions ci-dessous
    // Modifier les noms de variables. Ex : "nom" -> "prenom"

    // Remplacer ce commentaire par une première fonction

    // Remplacer ce commentaire par une deuxième fonction
});

form.section("Exemples", () => {
    // Variable texte
    form.text("nom", "Quel est votre nom ?");

    // Variable numérique
    form.number("age", "Quel est votre âge ?", {min: 0, max: 120});

    // Variable de choix (boutons)
    form.choice("sexe", "Quel est votre sexe ?", [["M", "Homme"], ["F", "Femme"]]);

    // Variable à choix unique (menu déroulant)
    form.dropdown("csp", "Quelle est votre CSP ?", [
        [1, "Agriculteur exploitant"],
        [2, "Artisan, commerçant ou chef d'entreprise"],
        [3, "Cadre ou profession intellectuelle supérieure"],
        [4, "Profession intermédiaire"],
        [5, "Employé"],
        [6, "Ouvrier"],
        [7, "Retraité"],
        [8, "Autre ou sans activité professionnelle"]
    ]);

    // Variable à choix unique (bouton radio)
    form.radio("lieu_vie", "Quel est votre lieu de vie ?", [
        ["maison", "Maison"],
        ["appartement", "Appartement"]
    ]);

    // Variable cases à cocher multiples
    form.multi("sommeil", "Présentez-vous un trouble du sommeil ?", [
        [1, "Troubles d’endormissement"],
        [2, "Troubles de maintien du sommeil"],
        [3, "Réveil précoce"],
        [4, "Sommeil non récupérateur"],
        [null, "Aucune de ces réponses"]
    ]);
});
`
        },

        {
            key: 'complicated',
            title: 'Formulaire compliqué',
            script: `form.pushOptions({large: true});

form.text("name", "Quel est votre nom ?");
form.number("age", "Quel est votre âge ?", {min: 0, max: 120,
                                            suffix: value => util.pluralFR(value, "an", "ans")});

let sexe = form.choice("sexe", "Quel est votre sexe ?", [["M", "Homme"], ["F", "Femme"]]);

form.dropdown("csp", "Quelle est votre CSP ?", [
    [1, "Agriculteur exploitant"],
    [2, "Artisan, commerçant ou chef d'entreprise"],
    [3, "Cadre ou profession intellectuelle supérieure"],
    [4, "Profession intermédiaire"],
    [5, "Employé"],
    [6, "Ouvrier"],
    [7, "Retraité"],
    [8, "Autre ou sans activité professionnelle"]
]);

form.radio("lieu_vie", "Quel est votre lieu de vie ?", [
    ["maison", "Maison"],
    ["appartement", "Appartement"]
]);

form.multi("sommeil", "Présentez-vous un trouble du sommeil ?", [
    [1, "Troubles d’endormissement"],
    [2, "Troubles de maintien du sommeil"],
    [3, "Réveil précoce"],
    [4, "Sommeil non récupérateur"],
    [null, "Aucune de ces réponses"]
]);

if (sexe.value == "F") {
    form.binary("enceinte", "Êtes-vous enceinte ?");
}

form.section("Alcool", () => {
    let alcool = form.binary("alcool", "Consommez-vous de l'alcool ?");

    if (alcool.value && form.value("enceinte")) {
        alcool.error("Pensez au bébé...");
        alcool.error("On peut mettre plusieurs erreurs");
        form.error("alcool", "Et de plein de manières différentes !");
    }

    if (alcool.value) {
        form.number("alcool_qt", "Combien de verres par semaine ?", {min: 1, max: 30});
    }
});

form.section("Autres", () => {
    form.number("enfants", "Combien avez-vous d'enfants ?", {min: 0, max: 30});
    form.binary("frites", "Aimez-vous les frites ?",
                {help: "Si si, c'est important, je vous le jure !"});
});

form.output(html\`On peut aussi mettre du <b>HTML directement</b> si on veut...
                 <button class="af_button" @click=\${e => go("complicated_help")}>Afficher l'aide</button>\`);
form.output("Ou bien encore mettre du <b>texte brut</b>.");
`
        },

        {
            key: 'complicated_help',
            title: 'Formulaire compliqué (aide)',
            script: `form.output("Loreum ipsum");

form.buttons([
    ["Donner l'alerte", () => alert("Alerte générale !!")],
    ["Revenir à l'auto-questionnaire", () => go("complicated")]
]);
`
        }
    ];
    this.pages.sort((page1, page2) => util.compareStrings(page1.key, page2.key));

    return this;
}).call({});