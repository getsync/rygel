if (shared.makeHeader)
    shared.makeHeader("Inventaire rapide de symptomatologie dépressive (QIDS)", page)
route.id = page.text("id", "Patient", {value: route.id, mandatory: true, compact: true,
                                       hidden: goupile.isLocked()}).value

if (shared.makeHeader)
    shared.makeFormFooter(nav, page)
