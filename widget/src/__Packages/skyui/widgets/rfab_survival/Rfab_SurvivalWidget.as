class skyui.widgets.rfab_survival.Rfab_SurvivalWidget extends skyui.widgets.WidgetBase
{
   var _built = false;
   var _main;              // the three bars; hidden while an item menu is open
   var _rows;
   var _prevFill;          // last pushed fill per row, for the per-tick delta
   var _tempIco;
   var _tempState = -1;
   var _inv;               // food bar shown over the inventory menu
   var _colorUI = true;
   var _autoHide = false;
   var _masterAlpha = 90;
   var _visTarget = -1;

   // geometry
   static var ICON = 16;
   static var ICON_GAP = 8;
   static var CAP_W = 19;      // knotwork end cap, one on each end of a bar
   static var CAP_OVER = 3;    // how far a cap laps back over the bar end
   static var BAR_X = 43;      // = ICON + ICON_GAP + CAP_W
   static var BAR_W = 128;
   static var BAR_H = 12;      // tall enough for the penalty number to sit in
   static var ROW_PITCH = 24;
   static var TEMP_ICON = 44;  // the temperature-feel icon, placed on its own
   static var TEXT_SIZE = 11;
   static var TEXT_PAD = 4;    // number inset from the right end of the bar
   static var TEMP_FADE = 0.5; // seconds, icon crossfade

   // Rows top to bottom: SLEEP, FOOD, COLD - so cold sits closest to the
   // bottom of the screen, food above it, sleep on top.
   static var ROW_SLEEP = 0;
   static var ROW_FOOD = 1;
   static var ROW_COLD = 2;

   // A tick's worth of change is a fraction of a pixel, so the delta marker
   // gets a floor: without it the direction of travel is invisible.
   static var DELTA_MIN_W = 2;
   static var DELTA_MIX = 0.5;   // how far the marker shifts to white / black

   // The HUD font RFAB maps in Interface\fontconfig.txt ($EverywhereFont ->
   // fritzq). Scaleform resolves the $-name itself, so nothing is embedded in
   // this .swf. If the numbers ever come out invisible, the font failed to
   // resolve - set EMBED_FONT to false and the player gets a device font.
   static var FONT = "$EverywhereFont";
   static var EMBED_FONT = true;

   // palette
   static var COL_MONO = 0xFFFFFF;
   static var COL_BARBG = 0x000000;    // recess under a bar
   static var COL_BEZEL = 0x8A867E;    // grey rim around a bar
   static var COL_BEZEL_LO = 0x2A2823; // its shadowed underside
   static var COL_NOTCH = 0xE8E0D0;
   static var COL_DANGER = 0xC65043;
   static var COL_TEXT = 0xEDE6D6;
   static var COL_KNOT = 0xC9C4B8;     // stone highlight along a knot strand
   static var COL_KNOT_DK = 0x171512;  // its casing
   static var COL_SLEEP = 0x8CA3C0;
   static var COL_HUNGER = 0xC49A5E;
   static var COL_COLD = 0x9AD0E0;
   static var TEMP_ICO = ["ico_temp0", "ico_temp1", "ico_temp2", "ico_temp3", "ico_temp4"];
   static var NEED_ICO = ["ico_sleep", "ico_food", "ico_cold"];

   function Rfab_SurvivalWidget()
   {
      super();
      this.build();
   }

   function onLoad()
   {
      this.build();
   }

   // WidgetLoader forwards HUD mode changes here. Hide only in menu modes;
   // stay visible while swimming / mounted / sneaking.
   function onModeChange(a_hudMode)
   {
      skse.Log("RSLHud.as: mode=" + a_hudMode);
      var hide = a_hudMode == "WorldMapMode" || a_hudMode == "JournalMode"
              || a_hudMode == "BookMode"     || a_hudMode == "InventoryMode"
              || a_hudMode == "BarterMode"   || a_hudMode == "MagicMode"
              || a_hudMode == "StatsMode"    || a_hudMode == "ContainerMode"
              || a_hudMode == "GiftMode"     || a_hudMode == "LockpickingMode";
      this._widgetHolder._visible = !hide;
   }

   // Papyrus (_RSL_Controller) drives this for the SkyUI item menus
   // (inventory / container / barter / gift). Those never send an onModeChange
   // the HUD filters on, so - unlike Magic / Map / Journal - the bars would
   // otherwise stay drawn over them.
   //
   // Only the bar block goes away. The inventory food preview is a sibling of
   // it, so it can stay up over the very menu that hid everything else.
   function setMenuHidden(a_hidden)
   {
      this.build();
      this._main._visible = !a_hidden;
   }

   function axisColor(a_i)
   {
      var C = skyui.widgets.rfab_survival.Rfab_SurvivalWidget;
      if (!this._colorUI) { return C.COL_MONO; }
      if (a_i == C.ROW_SLEEP) { return C.COL_SLEEP; }
      if (a_i == C.ROW_FOOD) { return C.COL_HUNGER; }
      return C.COL_COLD;
   }

   // build
   function build()
   {
      if (this._built) { return; }
      this._built = true;

      var C = skyui.widgets.rfab_survival.Rfab_SurvivalWidget;

      // No panel and no outer frame: the bars carry their own bezel and caps,
      // and a box around them only fought with the rest of the HUD.
      this._main = this.createEmptyMovieClip("rslMain", 20);

      this._rows = [];
      this._prevFill = [-1, -1, -1];
      var i = 0;
      while (i < 3)
      {
         this._rows[i] = this.buildBar(this._main, "rslRow" + i, i, 0, i * C.ROW_PITCH, true);
         i = i + 1;
      }

      // Temperature-feel icon: no longer part of the bar block. Papyrus places
      // it with setTempPos, so it can sit anywhere on the HUD.
      this._tempIco = this._main.createEmptyMovieClip("rslTemp", 90);

      // Inventory food preview, a sibling of _main so setMenuHidden leaves it
      // alone. Placed by setInvBar.
      this._inv = this.createEmptyMovieClip("rslInv", 40);
      this._inv._visible = false;
      this._inv.bar = this.buildBar(this._inv, "invRow", C.ROW_FOOD, 0, 0, false);

      this._alpha = 100;
   }

   // One bar: icon, bed, fill, delta marker, sheen, notch, knot caps, and
   // (on the HUD only) the penalty number.
   function buildBar(a_parent, a_name, a_axis, a_x, a_y, a_withLabel)
   {
      var C = skyui.widgets.rfab_survival.Rfab_SurvivalWidget;
      var row = a_parent.createEmptyMovieClip(a_name, a_parent.getNextHighestDepth());
      row._x = a_x;
      row._y = a_y;

      var icon = row.createEmptyMovieClip("icon", 1);
      icon._x = C.ICON * 0.5;
      icon._y = C.BAR_H * 0.5;
      var im = this.attachIcon(icon, C.NEED_ICO[a_axis], C.ICON);
      this.tint(im, this.axisColor(a_axis));

      // 1 - dark recess plus the grey rim around it
      var bg = row.createEmptyMovieClip("bg", 2);
      this.drawBarBed(bg, C.BAR_X, 0, C.BAR_W, C.BAR_H);

      // 2 - the coloured fill, redrawn every push
      var fill = row.createEmptyMovieClip("fill", 3);
      fill._x = C.BAR_X;

      // 3 - what changed since the last push, or what a meal would change
      var delta = row.createEmptyMovieClip("delta", 4);
      delta._x = C.BAR_X;

      // 4 - fixed pseudo-3D sheen over the whole bar: light on the top half,
      // shadow on the bottom. Drawn once and left alone; it reads as a rounded
      // surface no matter how far the fill has dropped.
      var gloss = row.createEmptyMovieClip("gloss", 5);
      this.drawGloss(gloss, C.BAR_X, 0, C.BAR_W, C.BAR_H);

      var notch = row.createEmptyMovieClip("notch", 6);

      // 5 - knotwork end caps, lapping back over the ends of the bar so they
      // read as clasps holding it
      var capH = C.BAR_H + 4;
      var capL = row.createEmptyMovieClip("capL", 7);
      capL._x = C.BAR_X + C.CAP_OVER;
      capL._y = C.BAR_H * 0.5;
      this.drawKnotCap(capL, -1, C.CAP_W + C.CAP_OVER, capH);
      var capR = row.createEmptyMovieClip("capR", 8);
      capR._x = C.BAR_X + C.BAR_W - C.CAP_OVER;
      capR._y = C.BAR_H * 0.5;
      this.drawKnotCap(capR, 1, C.CAP_W + C.CAP_OVER, capH);

      var label = undefined;
      if (a_withLabel) { label = this.makeLabel(row, "pen", C.BAR_X, C.BAR_W); }

      return {row:row, fill:fill, delta:delta, notch:notch, iconMc:im,
              label:label, axis:a_axis};
   }

   // update from Papyrus
   function setData(a_ss, a_sf, a_sn, a_hs, a_hf, a_hn, a_cs, a_cf, a_cn, a_au, a_al, a_tf, a_ci,
                    a_sp, a_hp, a_cp)
   {
      this.build();

      this._autoHide = a_au >= 0.5;
      this._masterAlpha = a_al;

      var col = a_ci >= 0.5;
      if (col != this._colorUI)
      {
         this._colorUI = col;
         this.recolor();
      }
      this.setTemp(a_tf);

      var shown0 = a_ss >= 0.5;
      var shown1 = a_hs >= 0.5;
      var shown2 = a_cs >= 0.5;
      var anyShown = shown0 || shown1 || shown2;

      var d0 = this.paintRow(0, shown0, a_sf, a_sn, a_sp);
      var d1 = this.paintRow(1, shown1, a_hf, a_hn, a_hp);
      var d2 = this.paintRow(2, shown2, a_cf, a_cn, a_cp);
      var anyDanger = d0 || d1 || d2;

      var wantShow = anyShown && ((!this._autoHide) || anyDanger);
      var tgt = 0;
      if (wantShow) { tgt = this._masterAlpha; }
      if (tgt != this._visTarget)
      {
         this._visTarget = tgt;
         this._alpha = tgt;
      }
   }

   function paintRow(a_i, a_shown, a_fill, a_safe, a_pen)
   {
      var C = skyui.widgets.rfab_survival.Rfab_SurvivalWidget;
      var r = this._rows[a_i];
      if (r == undefined) { return false; }
      r.row._visible = a_shown;
      if (!a_shown) { return false; }

      var f = this.clamp100(a_fill);
      var s = this.clamp100(a_safe);
      var danger = f < s;

      this.paintFill(r, f, this.axisColor(a_i));
      // the tick's own movement: lighter where the bar just refilled, darker
      // where it just drained
      this.paintDelta(r, this._prevFill[a_i], f, this.axisColor(a_i));
      this._prevFill[a_i] = f;
      this.paintNotch(r, s, danger);

      // penalty over the bar; nothing at all while the axis costs nothing
      var p = Math.round(a_pen);
      r.label.text = (p > 0) ? ("-" + p + "%") : "";
      r.label.textColor = danger ? C.COL_DANGER : C.COL_TEXT;

      return danger;
   }

   function paintFill(a_r, a_f, a_col)
   {
      var C = skyui.widgets.rfab_survival.Rfab_SurvivalWidget;
      a_r.fill.clear();
      this.paint(a_r.fill, 0, 0, C.BAR_W * (a_f / 100), C.BAR_H, a_col, 92);
   }

   // The marker between two fill levels. Rising (the bar refilled, i.e. the
   // need eased) shows 50% lighter at the leading edge; falling shows 50%
   // darker just past the new edge. a_from < 0 means "no previous value yet".
   function paintDelta(a_r, a_from, a_to, a_col)
   {
      var C = skyui.widgets.rfab_survival.Rfab_SurvivalWidget;
      a_r.delta.clear();
      if (a_from < 0 || a_from == a_to) { return; }

      var lo = Math.min(a_from, a_to);
      var hi = Math.max(a_from, a_to);
      var w = C.BAR_W * (hi - lo) / 100;
      if (w < C.DELTA_MIN_W) { w = C.DELTA_MIN_W; }

      var col = (a_to > a_from) ? this.mix(a_col, 0xFFFFFF, C.DELTA_MIX)
                                : this.mix(a_col, 0x000000, C.DELTA_MIX);
      this.paint(a_r.delta, C.BAR_W * lo / 100, 0, w, C.BAR_H, col, 95);
   }

   function paintNotch(a_r, a_s, a_danger)
   {
      var C = skyui.widgets.rfab_survival.Rfab_SurvivalWidget;
      var nCol = this._colorUI ? C.COL_NOTCH : C.COL_MONO;
      if (a_danger) { nCol = C.COL_DANGER; }
      a_r.notch._x = C.BAR_X + C.BAR_W * (a_s / 100);
      a_r.notch.clear();
      this.paint(a_r.notch, 0, -2, 2, C.BAR_H + 4, nCol, 90);
   }

   // --- separately placed pieces -------------------------------------------
   //
   // Papyrus works out where these belong: it knows the widget origin and the
   // widget scale, so it hands over coordinates already in this clip's space.

   function setTempPos(a_x, a_y, a_scale)
   {
      this.build();
      this._tempIco._x = a_x;
      this._tempIco._y = a_y;
      this._tempIco._xscale = a_scale;
      this._tempIco._yscale = a_scale;
   }

   // The food bar over the inventory menu. a_projected is where the bar would
   // land if the highlighted item were eaten: lighter ahead of the current fill
   // for a meal, darker behind it for something that will not stay down.
   function setInvBar(a_shown, a_fill, a_safe, a_projected, a_x, a_y, a_scale)
   {
      this.build();
      var C = skyui.widgets.rfab_survival.Rfab_SurvivalWidget;
      var shown = a_shown >= 0.5;
      this._inv._visible = shown;
      if (!shown) { return; }

      this._inv._x = a_x;
      this._inv._y = a_y;
      this._inv._xscale = a_scale;
      this._inv._yscale = a_scale;

      var f = this.clamp100(a_fill);
      var s = this.clamp100(a_safe);
      var b = this._inv.bar;
      this.paintFill(b, f, this.axisColor(C.ROW_FOOD));
      this.paintDelta(b, f, this.clamp100(a_projected), this.axisColor(C.ROW_FOOD));
      this.paintNotch(b, s, f < s);
   }

   function clamp100(a_v)
   {
      if (a_v < 0) { return 0; }
      if (a_v > 100) { return 100; }
      return a_v;
   }

   // Blend a colour towards a_towards by a_k (0..1), per channel.
   function mix(a_col, a_towards, a_k)
   {
      var r = ((a_col >> 16) & 0xFF) + (((a_towards >> 16) & 0xFF) - ((a_col >> 16) & 0xFF)) * a_k;
      var g = ((a_col >> 8) & 0xFF) + (((a_towards >> 8) & 0xFF) - ((a_col >> 8) & 0xFF)) * a_k;
      var b = (a_col & 0xFF) + ((a_towards & 0xFF) - (a_col & 0xFF)) * a_k;
      return (Math.round(r) << 16) | (Math.round(g) << 8) | Math.round(b);
   }

   function setScale(a_pct)
   {
      if (a_pct < 10) { a_pct = 10; }
      this._xscale = a_pct;
      this._yscale = a_pct;
   }

   function getWidth()
   {
      var C = skyui.widgets.rfab_survival.Rfab_SurvivalWidget;
      return C.BAR_X + C.BAR_W + C.CAP_W;
   }

   function getHeight()
   {
      var C = skyui.widgets.rfab_survival.Rfab_SurvivalWidget;
      return 2 * C.ROW_PITCH + C.BAR_H;
   }

   // drawing primitives
   function paint(a_mc, a_x, a_y, a_w, a_h, a_col, a_alpha)
   {
      a_mc.beginFill(a_col, a_alpha);
      a_mc.moveTo(a_x, a_y);
      a_mc.lineTo(a_x + a_w, a_y);
      a_mc.lineTo(a_x + a_w, a_y + a_h);
      a_mc.lineTo(a_x, a_y + a_h);
      a_mc.lineTo(a_x, a_y);
      a_mc.endFill();
   }

   function stroke(a_mc, a_x, a_y, a_w, a_h, a_thick, a_col, a_alpha)
   {
      a_mc.lineStyle(a_thick, a_col, a_alpha);
      a_mc.moveTo(a_x, a_y);
      a_mc.lineTo(a_x + a_w, a_y);
      a_mc.lineTo(a_x + a_w, a_y + a_h);
      a_mc.lineTo(a_x, a_y + a_h);
      a_mc.lineTo(a_x, a_y);
      a_mc.lineStyle();
   }

   // A rectangle filled with a vertical gradient of one colour fading out.
   // a_top true  -> opaque at the top edge, clear at the bottom (highlight)
   // a_top false -> clear at the top, opaque at the bottom (shadow)
   function gradient(a_mc, a_x, a_y, a_w, a_h, a_col, a_alpha, a_top)
   {
      var alphas = a_top ? [a_alpha, 0] : [0, a_alpha];
      a_mc.beginGradientFill("linear", [a_col, a_col], alphas, [0, 255],
                             {matrixType:"box", x:a_x, y:a_y, w:a_w, h:a_h, r:Math.PI / 2});
      a_mc.moveTo(a_x, a_y);
      a_mc.lineTo(a_x + a_w, a_y);
      a_mc.lineTo(a_x + a_w, a_y + a_h);
      a_mc.lineTo(a_x, a_y + a_h);
      a_mc.lineTo(a_x, a_y);
      a_mc.endFill();
   }

   // The bed a bar sits in: black recess, a grey rim around it, and a dark
   // inner line so the rim reads as raised rather than painted on.
   function drawBarBed(a_mc, a_x, a_y, a_w, a_h)
   {
      var C = skyui.widgets.rfab_survival.Rfab_SurvivalWidget;
      this.paint(a_mc, a_x, a_y, a_w, a_h, C.COL_BARBG, 70);
      this.stroke(a_mc, a_x - 1, a_y - 1, a_w + 2, a_h + 2, 1.5, C.COL_BEZEL, 90);
      this.stroke(a_mc, a_x, a_y, a_w, a_h, 1, C.COL_BEZEL_LO, 80);
   }

   // Pseudo-3D: a white sheen down the top half and a black one up from the
   // bottom, both fading to nothing at the middle.
   function drawGloss(a_mc, a_x, a_y, a_w, a_h)
   {
      var half = a_h * 0.5;
      this.gradient(a_mc, a_x, a_y, a_w, half, 0xFFFFFF, 55, true);
      this.gradient(a_mc, a_x, a_y + half, a_w, half, 0x000000, 45, false);
   }

   // --- knotwork end caps -------------------------------------------------
   //
   // Drawn, not imported: the motif is a handful of straight strands, and
   // vectors stay crisp at whatever scale the player sets the widget to, where
   // a 16px bitmap would not.
   //
   // Two nested chevrons pointing away from the bar, each a light core stroked
   // inside a dark casing - the same outlined-ribbon look the RFAB bars have.
   // Earlier drafts wove a third strand through them; at the size this renders
   // (~19px) the extra crossing turned to mush, and the plain double chevron
   // both reads better and matches the silhouette it is copying.
   //
   // a_dir -1 points the cap away to the left, +1 to the right. The origin is
   // the end of the bar, centred on its height.
   function drawKnotCap(a_mc, a_dir, a_w, a_h)
   {
      var hy = a_h * 0.5;
      // inner strand is the lighter of the two, so the pair reads as depth
      this.ribbon(a_mc, this.chevron(a_dir, a_w, hy, 0.58, 0.00), 1.2);
      this.ribbon(a_mc, this.chevron(a_dir, a_w, hy, 1.00, 0.42), 1.5);
   }

   // One chevron as a flat point list: arms at a_base, tip at a_tip, both given
   // as a fraction of the cap width.
   function chevron(a_dir, a_w, a_hy, a_tip, a_base)
   {
      return [a_dir * a_w * a_base, -a_hy,
              a_dir * a_w * a_tip,   0,
              a_dir * a_w * a_base,  a_hy];
   }

   // One strand: dark casing first, light core on top of it.
   function ribbon(a_mc, a_pts, a_thick)
   {
      var C = skyui.widgets.rfab_survival.Rfab_SurvivalWidget;
      this.polyline(a_mc, a_pts, a_thick + 2.4, C.COL_KNOT_DK, 100);
      this.polyline(a_mc, a_pts, a_thick, C.COL_KNOT, 100);
   }

   // Flat [x0,y0, x1,y1, ...] so the callers above stay readable.
   function polyline(a_mc, a_pts, a_thick, a_col, a_alpha)
   {
      a_mc.lineStyle(a_thick, a_col, a_alpha, true, "normal", "round", "round");
      a_mc.moveTo(a_pts[0], a_pts[1]);
      var i = 2;
      while (i < a_pts.length)
      {
         a_mc.lineTo(a_pts[i], a_pts[i + 1]);
         i = i + 2;
      }
      a_mc.lineStyle();
   }

   // Right-aligned number sitting on top of a bar, in the game's own HUD font.
   function makeLabel(a_parent, a_name, a_x, a_w)
   {
      var C = skyui.widgets.rfab_survival.Rfab_SurvivalWidget;
      var tf = a_parent.createTextField(a_name, 10, a_x, -2, a_w - C.TEXT_PAD, C.BAR_H + 4);
      tf.selectable = false;
      tf.mouseWheelEnabled = false;
      tf.embedFonts = C.EMBED_FONT;
      tf.antiAliasType = "advanced";

      var fmt = new TextFormat();
      fmt.font = C.FONT;
      fmt.size = C.TEXT_SIZE;
      fmt.align = "right";
      fmt.bold = true;
      tf.setNewTextFormat(fmt);

      // A dark outline keeps the number readable over a light fill.
      var f = new flash.filters.DropShadowFilter(1, 90, 0x000000, 1, 2, 2, 1, 2);
      tf.filters = [f];

      tf.text = "";
      return tf;
   }

   // Attach an embedded bitmap ("ico_*") to a_parent, centered on (0,0), fitted
   // to a_size px. Returns the holder clip (for tint()). Unique clip name so two
   // can coexist during a crossfade.
   function attachIcon(a_parent, a_name, a_size)
   {
      var bd = flash.display.BitmapData.loadBitmap(a_name);
      if (bd == undefined) { return undefined; }
      var dp = a_parent.getNextHighestDepth();
      var mc = a_parent.createEmptyMovieClip("i" + dp, dp);
      mc.attachBitmap(bd, 0, "auto", true);
      var sc = a_size / bd.width;
      mc._xscale = sc * 100;
      mc._yscale = sc * 100;
      mc._x = -a_size * 0.5;
      mc._y = -a_size * 0.5;
      return mc;
   }

   // Recolor a white glyph clip to a solid RGB, keeping its alpha silhouette.
   function tint(a_mc, a_col)
   {
      if (a_mc == undefined) { return; }
      var c = new Color(a_mc);
      c.setTransform({ra:0, ga:0, ba:0, aa:100,
                      rb:(a_col >> 16) & 0xFF, gb:(a_col >> 8) & 0xFF, bb:a_col & 0xFF, ab:0});
   }

   // Re-tint the needs icons after a colour-mode flip. Bars and notches fix
   // themselves on the paintRow calls right after this in setData().
   function recolor()
   {
      var i = 0;
      while (i < 3)
      {
         this.tint(this._rows[i].iconMc, this.axisColor(i));
         i = i + 1;
      }
      this.tint(this._inv.bar.iconMc, this.axisColor(1));
      this.tintTemp();
   }

   // The temperature icons carry their own colours, unlike the white needs
   // glyphs, so colour mode leaves them alone and only the mono setting
   // flattens them to white.
   function tintTemp()
   {
      var d = this._tempIco;
      var k;
      for (k in d)
      {
         if (typeof(d[k]) != "movieclip") { continue; }
         if (this._colorUI)
         {
            new Color(d[k]).setTransform({ra:100, ga:100, ba:100, aa:100,
                                          rb:0, gb:0, bb:0, ab:0});
         }
         else
         {
            this.tint(d[k], skyui.widgets.rfab_survival.Rfab_SurvivalWidget.COL_MONO);
         }
      }
   }

   // Swap the temperature-feel icon (0 cold-fast .. 4 warm-fast). Crossfades
   // over TEMP_FADE s. No-op if unchanged.
   function setTemp(a_n)
   {
      var C = skyui.widgets.rfab_survival.Rfab_SurvivalWidget;
      if (a_n < 0) { a_n = 0; }
      if (a_n > 4) { a_n = 4; }
      if (a_n == this._tempState) { return; }
      this._tempState = a_n;

      var d = this._tempIco;
      var k;
      for (k in d) { if (typeof(d[k]) == "movieclip") { this.fadeOutRemove(d[k]); } }

      var mc = this.attachIcon(d, C.TEMP_ICO[a_n], C.TEMP_ICON);
      if (mc == undefined) { return; }
      if (!this._colorUI) { this.tint(mc, C.COL_MONO); }
      mc._alpha = 0;
      new mx.transitions.Tween(mc, "_alpha", mx.transitions.easing.None.easeNone,
                               0, 100, C.TEMP_FADE, true);
   }

   function fadeOutRemove(a_mc)
   {
      var C = skyui.widgets.rfab_survival.Rfab_SurvivalWidget;
      var tw = new mx.transitions.Tween(a_mc, "_alpha", mx.transitions.easing.None.easeNone,
                                        a_mc._alpha, 0, C.TEMP_FADE, true);
      tw.onMotionFinished = function() { a_mc.removeMovieClip(); };
   }
}
