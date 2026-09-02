class skyui.widgets.rfab_survival.Rfab_SurvivalWidget extends skyui.widgets.WidgetBase
{
   var _built = false;
   var _rows;
   var _frame;
   var _tempIco;
   var _tempState = -1;
   var _colorUI = true;
   var _autoHide = false;
   var _masterAlpha = 90;
   var _visTarget = -1;

   // geometry
   static var PAD = 7;
   static var ICON = 15;
   static var ICON_GAP = 8;
   static var BAR_X = 23;      // = ICON + ICON_GAP
   static var BAR_W = 128;
   static var BAR_H = 8;
   static var ROW_PITCH = 19;
   static var TEMP_GAP = 13;   // bars -> temperature icon
   static var TEMP_ICON = 39;  // x1.5 - reads clearly as the state indicator
   static var RIGHT_EXTRA = 8; // extra frame past the temperature icon
   static var TEMP_FADE = 0.5; // seconds, icon crossfade

   // palette
   static var COL_FRAME = 0xFFFFFF;    // common frame around bars + icons
   static var COL_FRAME_HI = 0xFFFFFF;
   static var COL_MONO = 0xFFFFFF;
   static var COL_PANEL = 0x0B0B0D;
   static var COL_BARBG = 0x000000;
   static var COL_NOTCH = 0xE8E0D0;
   static var COL_DANGER = 0xC65043;
   static var COL_SLEEP = 0x8CA3C0;
   static var COL_HUNGER = 0xC49A5E;
   static var COL_COLD = 0x9AD0E0;
   static var TEMP_ICO = ["ico_temp0", "ico_temp1", "ico_temp2", "ico_temp3", "ico_temp4"];
   static var TEMP_COL = [0x7FB8E0, 0x9AD0E0, 0xE8E0D0, 0xE0A860, 0xE07840];
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

   function axisColor(a_i)
   {
      var C = skyui.widgets.rfab_survival.Rfab_SurvivalWidget;
      if (!this._colorUI) { return C.COL_MONO; }
      if (a_i == 0) { return C.COL_SLEEP; }
      if (a_i == 1) { return C.COL_HUNGER; }
      return C.COL_COLD;
   }

   // build
   function build()
   {
      if (this._built) { return; }
      this._built = true;

      var C = skyui.widgets.rfab_survival.Rfab_SurvivalWidget;
      var innerW = C.BAR_X + C.BAR_W + C.TEMP_GAP + C.TEMP_ICON;
      var innerH = 2 * C.ROW_PITCH + C.BAR_H;

      // frame + backing panel (extended a touch past the temperature icon)
      this._frame = this.createEmptyMovieClip("rslFrame", 5);
      this.drawPanel(this._frame, -C.PAD, -C.PAD - 3, innerW + C.PAD * 2 + C.RIGHT_EXTRA, innerH + C.PAD * 2 + 3);

      // rows
      this._rows = [];
      var i = 0;
      while (i < 3)
      {
         var row = this.createEmptyMovieClip("rslRow" + i, 20 + i);
         row._x = 0;
         row._y = i * C.ROW_PITCH;

         var icon = row.createEmptyMovieClip("icon", 1);
         icon._x = C.ICON * 0.5;
         icon._y = C.BAR_H * 0.5;
         var im = this.attachIcon(icon, C.NEED_ICO[i], C.ICON);
         this.tint(im, this.axisColor(i));

         var bg = row.createEmptyMovieClip("bg", 2);
         this.paint(bg, C.BAR_X, 0, C.BAR_W, C.BAR_H, C.COL_BARBG, 45);
         this.stroke(bg, C.BAR_X, 0, C.BAR_W, C.BAR_H, 1, C.COL_FRAME, 55);

         var fill = row.createEmptyMovieClip("fill", 3);
         fill._x = C.BAR_X;
         this.paint(fill, 0, 0, C.BAR_W * 0.6, C.BAR_H, this.axisColor(i), 92);

         var notch = row.createEmptyMovieClip("notch", 4);
         notch._x = C.BAR_X + C.BAR_W * 0.75;
         this.paint(notch, 0, -2, 2, C.BAR_H + 4, C.COL_NOTCH, 85);

         this._rows[i] = {row:row, fill:fill, notch:notch, icon:icon, iconMc:im};
         i = i + 1;
      }

      // temperature-feel icon: one slot, right of the bars, vertically centered
      this._tempIco = this.createEmptyMovieClip("rslTemp", 30);
      this._tempIco._x = C.BAR_X + C.BAR_W + C.TEMP_GAP + C.TEMP_ICON * 0.5;
      this._tempIco._y = innerH * 0.5;

      this._alpha = 100;
   }

   // update from Papyrus
   function setData(a_ss, a_sf, a_sn, a_hs, a_hf, a_hn, a_cs, a_cf, a_cn, a_au, a_al, a_tf, a_ci)
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

      var d0 = this.paintRow(0, shown0, a_sf, a_sn);
      var d1 = this.paintRow(1, shown1, a_hf, a_hn);
      var d2 = this.paintRow(2, shown2, a_cf, a_cn);
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

   function paintRow(a_i, a_shown, a_fill, a_safe)
   {
      var C = skyui.widgets.rfab_survival.Rfab_SurvivalWidget;
      var r = this._rows[a_i];
      if (r == undefined) { return false; }
      r.row._visible = a_shown;
      if (!a_shown) { return false; }

      var f = a_fill;
      if (f < 0) { f = 0; }
      if (f > 100) { f = 100; }

      var s = a_safe;
      if (s < 0) { s = 0; }
      if (s > 100) { s = 100; }

      var danger = f < s;

      // fill is always the axis's own color
      r.fill.clear();
      this.paint(r.fill, 0, 0, C.BAR_W * (f / 100), C.BAR_H, this.axisColor(a_i), 92);

      // notch marks the threshold; turns red when fill drops below it
      var nCol = this._colorUI ? C.COL_NOTCH : C.COL_MONO;
      if (danger) { nCol = C.COL_DANGER; }
      r.notch._x = C.BAR_X + C.BAR_W * (s / 100);
      r.notch.clear();
      this.paint(r.notch, 0, -2, 2, C.BAR_H + 4, nCol, 90);

      return danger;
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
      return C.BAR_X + C.BAR_W + C.TEMP_GAP + C.TEMP_ICON + C.PAD * 2 + C.RIGHT_EXTRA;
   }

   function getHeight()
   {
      var C = skyui.widgets.rfab_survival.Rfab_SurvivalWidget;
      return 2 * C.ROW_PITCH + C.BAR_H + C.PAD * 2 + 3;
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

   // backing panel + double frame (dark outer + bone highlight inside)
   function drawPanel(a_mc, a_x, a_y, a_w, a_h)
   {
      var C = skyui.widgets.rfab_survival.Rfab_SurvivalWidget;
      this.paint(a_mc, a_x, a_y, a_w, a_h, C.COL_PANEL, 42);
      this.stroke(a_mc, a_x, a_y, a_w, a_h, 1.5, C.COL_FRAME, 85);
      this.stroke(a_mc, a_x + 2, a_y + 2, a_w - 4, a_h - 4, 1, C.COL_FRAME_HI, 30);
   }

   // Attach an embedded bitmap ("ico_*", 64x64 white glyph on alpha) to a_parent,
   // centered on (0,0), fitted to a_size px. Returns the holder clip (for tint()).
   // Unique clip name so two can coexist during a crossfade.
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

   // Re-tint the fixed icons after a colour-mode flip. Bars/notches fix
   // themselves on the paintRow calls right after this in setData().
   function recolor()
   {
      var i = 0;
      while (i < 3)
      {
         this.tint(this._rows[i].iconMc, this.axisColor(i));
         i = i + 1;
      }
      var col = this._colorUI
              ? skyui.widgets.rfab_survival.Rfab_SurvivalWidget.TEMP_COL[this._tempState]
              : skyui.widgets.rfab_survival.Rfab_SurvivalWidget.COL_MONO;
      var d = this._tempIco;
      for (var k in d) { if (typeof(d[k]) == "movieclip") { this.tint(d[k], col); } }
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
      this.tint(mc, this._colorUI ? C.TEMP_COL[a_n] : C.COL_MONO);
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
