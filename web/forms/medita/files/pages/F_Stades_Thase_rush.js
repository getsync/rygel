if (shared.makeHeader)
    shared.makeHeader("Niveau de résistance aux ATD (Thase & Rush)", page)
route.id = page.text("id", "Patient", {value: route.id, mandatory: true, compact: true}).value

form.pushOptions({mandatory: true, missing_mode: 'disable'})

form.section("Niveau de résistance", () => {
    form.enumRadio("stadethaserush", "", [
        [1, "Stade 1 : Echec à au moins un traitement antidépresseur utilisé à doses suffisantes et pendant une durée adéquate (ex: IRS)."],
        [2, "Stade 2	: Stade 1 + échec à un autre antidépresseur d’une autre classe que celle utilisée par dans le Stade 1."],
        [3, "Stade 3	: Stade 2 + échec à un antidépresseur tricyclique."],
        [4, "Stade 4	: Stade 3 + échec à un inhibiteur de la monoamine oxydase."],
	[5, "Stade 5	: Stade 4 + échec de l’électroconvulsivothérapie bilatérale."],
          ])
})