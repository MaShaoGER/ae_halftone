(function () {
    if (app.project.renderQueue.numItems !== 0) throw new Error("Render queue must be empty before validation.");
    var star = null, profile = null;
    for (var i = 1; i <= app.project.numItems; i++) {
        var item = app.project.item(i);
        if (item.name === "star.png") star = item;
        if (item.name === "gradient-profile.png") profile = item;
    }
    if (!star || !profile) throw new Error("Demo assets are missing.");
    var c = app.project.items.addComp("__有序图案_多帧验证", 1920, 1080, 1, 1, 25);
    var src = c.layers.add(star); src.enabled = false;
    var map = c.layers.add(profile); map.enabled = false;
    for (var k = 0; k < 2; k++) {
        var layer = c.layers.addSolid([0, 0, 0], "验证生成器 " + k, 1920, 1080, 1, 1);
        layer.property("ADBE Effect Parade").addProperty("com.mashao.OrderedPatterns");
        var e = layer.property("ADBE Effect Parade").property(1);
        e.property(1).setValue(k === 0 ? 1 : 4);
        e.property(4).setValue(k === 0 ? 32 : 26);
        e.property(5).setValue(k === 0 ? 80 : 70);
        e.property(6).setValue(k === 0 ? 36 : 30);
        e.property(7).setValue(k === 0 ? 8 : 21);
        e.property(11).setValueAtTime(0, 0);
        e.property(11).setValueAtTime(.96, 80);
        e.property(12).setValueAtTime(0, 0);
        e.property(12).setValueAtTime(.96, 25);
        if (k === 0) {
            e.property(15).setValue([.15, .6, 1]);
            e.property(16).setValue(45);
        }
    }
    // Reacquire layer indices after all layers have been inserted.
    for (var j = 1; j <= c.numLayers; j++) {
        var layer = c.layer(j);
        if (layer.name === "验证生成器 1") {
            var e = layer.property("ADBE Effect Parade").property(1);
            e.property(2).setValue(src.index);
            e.property(3).setValue(map.index);
        }
    }
    var rq = app.project.renderQueue.items.add(c);
    rq.timeSpanStart = 0;
    rq.timeSpanDuration = .96;
    return JSON.stringify({compId: c.id, compName: c.name, oldBitDepth: app.project.bitsPerChannel,
        templates: rq.outputModule(1).templates, settings: rq.outputModule(1).getSettings(GetSettingsFormat.STRING)});
}());
