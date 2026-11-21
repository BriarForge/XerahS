#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace xerahs::app {

// Crisp Lucide-matched vector icons rendered with QPainter on a 24x24 grid.
// Using QIcon instead of private-use-area font glyphs avoids binary font blobs,
// renders cleanly at every DPI on Wayland/X11/offscreen, and keeps QLabel text
// purely translatable prose.
enum class AppIcon {
  // Host navigation (HostIcons.cs)
  Camera,
  Film,
  Pencil,
  HardDrive,
  ChartColumn,
  Combine,
  CloudUpload,
  WandSparkles,
  SlidersVertical,
  Terminal,
  Aperture,

  // Host actions & sections
  Search,
  ChevronDown,
  ChevronRight,
  Play,
  Square,
  Pause,
  RotateCcw,
  CloseX,
  TriangleAlert,
  Link,
  Users,
  ExternalLink,
  Plus,
  RefreshCw,
  Share2,
  List,
  LayoutGrid,
  FolderOpen,
  FilePlus,
  Command,
  Palette,
  QrCode,
  ScanLine,
  FileText,
  Hash,
  ImageIcon,
  Scissors,
  Sparkles,

  // Editor tools & actions (EditorIcons.cs)
  MousePointer2,
  Circle,
  Minus,
  ArrowRight,
  Highlighter,
  Type,
  MessageSquare,
  MousePointer,
  Droplet,
  Grid2x2,
  Lightbulb,
  Eraser,
  Crop,
  Smile,
  Undo2,
  Redo2,
  Trash2,
  BrushCleaning,
  Clipboard,
  Save,
  SaveAll,
  Pin,
  Printer,
  Wallpaper,
  Layers2,
  ZoomIn,
  ZoomOut,
  Maximize,
  RotateCw,
  Settings,
  LogOut,
  Check,
};

QIcon appIcon(AppIcon icon, const QColor &color, int size = 18);
QIcon swatchIcon(const QColor &color, bool hollowBorder = false, int size = 18);

}  // namespace xerahs::app
