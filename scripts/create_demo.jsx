// Run in an empty AE project. Asset paths are relative to this script.
(function () {
    if (!app.project || app.project.numItems !== 0) throw new Error("Use an empty project for the Ordered Patterns demo.");
    var root = new File($.fileName).parent.parent;
    var match = "com.mashao.OrderedPatterns";
    var star = app.project.importFile(new ImportOptions(new File(root.fsName + "/examples/assets/star.png")));
    var profile = app.project.importFile(new ImportOptions(new File(root.fsName + "/examples/assets/gradient-profile.png")));
    var created = [];
    function make(name, shape, color) {
        var c = app.project.items.addComp(name, 960, 540, 1, 5, 25);
        var generator = c.layers.addSolid([0, 0, 0], "Pattern Generator", 960, 540, 1, 5);
        var group = generator.property("ADBE Effect Parade");
        if (!group.canAddProperty(match)) throw new Error("Ordered Patterns is not loaded. Install the plugin and restart AE.");
        group.addProperty(match);
        var e = generator.property("ADBE Effect Parade").property(1);
        e.property(1).setValue(shape);
        e.property(4).setValue(22);
        e.property(5).setValue(70);
        e.property(6).setValue(36);
        e.property(7).setValue(12);
        e.property(15).setValue(color);
        created.push({id: c.id, name: c.name});
        return c;
    }
    app.beginUndoGroup("Create Ordered Patterns demo");
    try {
        var circle = make("01 - Circles - Linear Spacing", 1, [0.25, 0.8, 1]);
        var diamond = make("02 - Diamonds - Staggered", 3, [0.7, 0.45, 1]);
        var e = diamond.layer(1).property("ADBE Effect Parade").property(1);
        e.property(7).setValue(19);
        e.property(10).setValue(20);
        e.property(14).setValue(50);
        var custom = make("03 - Custom Star - Gradient Input", 4, [1, 1, 1]);
        var source = custom.layers.add(star); source.name = "CUSTOM - star.png"; source.enabled = false;
        var gradient = custom.layers.add(profile); gradient.name = "PROFILE - black to white"; gradient.enabled = false;
        e = custom.layer("Pattern Generator").property("ADBE Effect Parade").property(1);
        e.property(2).setValue(source.index);
        e.property(3).setValue(gradient.index);
        e.property(4).setValue(32);
        e.property(5).setValue(96);
        e.property(6).setValue(42);
        e.property(7).setValue(27);
        e.property(11).setValueAtTime(0, 0);
        e.property(11).setValueAtTime(4, 45);
        custom.layer("Pattern Generator").selected = true;
        custom.openInViewer();
        custom.time = 0;
    } finally {
        app.endUndoGroup();
    }
    return JSON.stringify({compositions: created, projectItems: app.project.numItems});
}());
