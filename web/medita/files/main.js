// Échelles

shared.echelles = [
    {category: "Général", name: "Observation", form: "F_OBSERVATION"},

    {category: "AUTO-QUESTIONNAIRES", name: "Inventaire de dépression de Beck (BDI)", form: "F_BDI"},
    {category: "AUTO-QUESTIONNAIRES", name: "Échelle de dépression QIDS", form: "F_QIDS"},
    {category: "AUTO-QUESTIONNAIRES", name: "Questionnaire de trouble de l'humeur (MDQ)", form: "F_MDQ"},
    {category: "AUTO-QUESTIONNAIRES", name: "Auto-questionnaire de Angst", form: "F_ANGST"},
    {category: "AUTO-QUESTIONNAIRES", name: "Inventaire de Beck pour l'anxiété", form: "F_IBA"},
    {category: "AUTO-QUESTIONNAIRES", name: "Chilhood Trauma Questionnaire (CTQ)", form: "F_CTQ"},
    {category: "AUTO-QUESTIONNAIRES", name: "Echelle de mesure de l'observance médicamenteuse (MARS)", form: "F_MARS"},
    {category: "AUTO-QUESTIONNAIRES", name: "Questionnaire de fonctionnement social", form: "F_QFS"},
    {category: "AUTO-QUESTIONNAIRES", name: "PQD4", form: "F_PDQ4"},
    {category: "AUTO-QUESTIONNAIRES", name: "Symptômes psychotiques", form: "F_symptomespsychotiquesMINI"},


    {category: "Catatonie", name: "Catatonie : Critères diagnostiques", form: "F_CATATONIE"},
    {category: "Catatonie", name: "Échelle de Bush-Francis (BFCRS)", form: "F_BFCRS"},


    {category: "ESPPER", name: "MADRS", form: "F_MADRS"},
    {category: "ESPPER", name: "Historique des traitements", form: "F_Chimiogramme"},
    {category: "ESPPER", name: "État de stress post-traumatique (PCLS)", form: "F_PCLS"},
    {category: "ESPPER", name: "Évaluation de la dépression psychotique (PDAS)", form: "F_PDAS"},
    {category: "ESPPER", name: "Évaluation globale du fonctionnement (EGF)", form: "F_EGF"},
    {category: "ESPPER", name: "Echelle de Calgary (CDS)", form: "F_CDSS"},
    {category: "ESPPER", name: "Echelle abrégée d'évaluation psychiatrique (BPRS)", form: "BPRS"},
    {category: "ESPPER", name: "Questionnaire de Altman (ASRM)", form: "F_ALTMAN"},


    {category: "Personnalité", name: "Echelle SAPAS (Standardised Assessment of Personality – Abbreviated Scale) REVOIR SCORE", form: "SAPAS"},

    {category: "Fonctionnement", name: "Échelle brève d'évaluation du fonctionnement du patient (FAST)", form: "FAST"},
    {category: "Fonctionnement", name: "Évaluation globale du fonctionnement (EGF)", form: "F_EGF"},

    {category: "Neuropsy", name: "MOCA", form: "F_MOCA"},
    {category: "Neuropsy", name: "Barnes Akathisia Rating Scale", form: "F_BARS"},
    {category: "Neuropsy", name: "Historique des traitements", form: "F_Chimiogramme_neuropsy"},

    {category: "CNEP/Troubles somatoformes", name: "PQD4", form: "F_PDQ4"},
    {category: "CNEP/Troubles somatoformes", name: "Chilhood Trauma Questionnaire (CTQ)", form: "F_CTQ"},
    {category: "CNEP/Troubles somatoformes", name: "État de stress post-traumatique (PCLS)", form: "F_PCLS"},
]

// Déclaration des pages

app.pushOptions({
    default_actions: false,
    float_actions: false
})

app.form("Accueil", form => {
    form.page("Accueil", "Patients")
    form.page("Exports", "Données")
})
app.form("Patient", form => {
    form.idHandler = () => route.ctx || util.makeULID();

    form.page("Informations")
    form.page("Suivi")
    form.page("Formulaires", "Échelles")
    form.page("Courriers")
})

{
    let handled_set = new Set;

    for (let echelle of shared.echelles) {
        if (handled_set.has(echelle.form))
            continue;

        app.form(echelle.form, echelle.name)
        handled_set.add(echelle.form)
    }
}

// Templates

shared.makeHeader = function(title, page) {
    if (!goupile.isLocked()) {
        page.output(html`
            <a href=${env.base_url}><img src="${env.base_url}favicon.png" height="48"
                                         style="position: absolute; top: 12px; right: 12px; z-index: 99;"></a>
        `)
    }
}

shared.makeFormFooter = function(nav, page) {
    page.output(html`
        <br/>
        <button class="md_button" ?disabled=${!page.hasChanged() || !page.isValid()}
                @click=${page.submit}>Enregistrer</button>
        ${!goupile.isLocked() ?
            html`<button class="md_button"
                         @click=${e => go(nav.link("Patient/Suivi", {id: route.ctx}))}>Retourner au patient</button>` : ''}
    `)
}
