class skyui.widgets.rfab_survival.Rfab_SurvivalWidget extends skyui.widgets.WidgetBase
{
   var _built = false;
   var _rows;
   var _frame;
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

   // palette (Skyrim bone tones)
   static var COL_FRAME = 0x6B5F49;
   static var COL_FRAME_HI = 0xB6A784;
   static var COL_PANEL = 0x0B0B0D;
   static var COL_BARBG = 0x000000;
   static var COL_NOTCH = 0xE8E0D0;
   static var COL_DANGER = 0xC65043;
   static var COL_SLEEP = 0x8CA3C0;
   static var COL_HUNGER = 0xC49A5E;
   static var COL_COLD = 0x9AD0E0;

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
      if (a_i == 0) { return skyui.widgets.rfab_survival.Rfab_SurvivalWidget.COL_SLEEP; }
      if (a_i == 1) { return skyui.widgets.rfab_survival.Rfab_SurvivalWidget.COL_HUNGER; }
      return skyui.widgets.rfab_survival.Rfab_SurvivalWidget.COL_COLD;
   }

   // build
   function build()
   {
      if (this._built) { return; }
      this._built = true;

      var C = skyui.widgets.rfab_survival.Rfab_SurvivalWidget;
      var innerW = C.BAR_X + C.BAR_W;
      var innerH = 2 * C.ROW_PITCH + C.BAR_H;

      // frame + backing panel
      this._frame = this.createEmptyMovieClip("rslFrame", 5);
      this.drawPanel(this._frame, -C.PAD, -C.PAD - 3, innerW + C.PAD * 2, innerH + C.PAD * 2 + 3);

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
         this.drawIcon(icon, i, this.axisColor(i));

         var bg = row.createEmptyMovieClip("bg", 2);
         this.paint(bg, C.BAR_X, 0, C.BAR_W, C.BAR_H, C.COL_BARBG, 45);
         this.stroke(bg, C.BAR_X, 0, C.BAR_W, C.BAR_H, 1, C.COL_FRAME, 55);

         var fill = row.createEmptyMovieClip("fill", 3);
         fill._x = C.BAR_X;
         this.paint(fill, 0, 0, C.BAR_W * 0.6, C.BAR_H, this.axisColor(i), 92);

         var notch = row.createEmptyMovieClip("notch", 4);
         notch._x = C.BAR_X + C.BAR_W * 0.75;
         this.paint(notch, 0, -2, 2, C.BAR_H + 4, C.COL_NOTCH, 85);

         this._rows[i] = {row:row, fill:fill, notch:notch, icon:icon};
         i = i + 1;
      }

      this._alpha = 100;
   }

   // update from Papyrus
   function setData(a_ss, a_sf, a_sn, a_hs, a_hf, a_hn, a_cs, a_cf, a_cn, a_au, a_al)
   {
      this.build();

      this._autoHide = a_au >= 0.5;
      this._masterAlpha = a_al;

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
      var nCol = C.COL_NOTCH;
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
      return C.BAR_X + C.BAR_W + C.PAD * 2;
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

   // stroked axis icon, centered at (0,0), radius ~6.5
   function drawIcon(a_mc, a_i, a_col)
   {
      var r = 6.5;
      a_mc.lineStyle(1.5, a_col, 95);
      if (a_i == 0)
      {
         // moon: crescent from two arcs
         a_mc.moveTo(0, -r);
         a_mc.curveTo(r * 1.4, 0, 0, r);
         a_mc.curveTo(r * 0.45, 0, 0, -r);
      }
      else if (a_i == 1)
      {
         // fork
         a_mc.moveTo(-3.4, -r);
         a_mc.lineTo(-3.4, -1);
         a_mc.moveTo(0, -r);
         a_mc.lineTo(0, -1);
         a_mc.moveTo(3.4, -r);
         a_mc.lineTo(3.4, -1);
         a_mc.moveTo(-3.4, -1);
         a_mc.lineTo(3.4, -1);
         a_mc.moveTo(0, -1);
         a_mc.lineTo(0, r);
      }
      else
      {
         // snowflake: 3 axes (6 spokes) + end barbs
         this.snowAxis(a_mc, 0, 1, r);
         this.snowAxis(a_mc, 0.866, 0.5, r);
         this.snowAxis(a_mc, 0.866, -0.5, r);
      }
      a_mc.lineStyle();
   }

   function snowAxis(a_mc, a_ux, a_uy, a_r)
   {
      var tx = a_ux * a_r;
      var ty = a_uy * a_r;
      // spoke
      a_mc.moveTo(-tx, -ty);
      a_mc.lineTo(tx, ty);
      // barbs at both ends, across the spoke
      var px = -a_uy;
      var py = a_ux;
      var k = a_r * 0.42;
      var m = 0.6;
      a_mc.moveTo(tx * m - px * k, ty * m - py * k);
      a_mc.lineTo(tx * m + px * k, ty * m + py * k);
      a_mc.moveTo(-tx * m - px * k, -ty * m - py * k);
      a_mc.lineTo(-tx * m + px * k, -ty * m + py * k);
   }
}
