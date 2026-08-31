#include <QGuiApplication>
#include <QSGNode>

#include "visualization/spectrum_waterfall_item.hpp"

namespace {

class TestableSpectrumWaterfallItem final
    : public cwassistant::desktop::SpectrumWaterfallItem {
 public:
  using SpectrumWaterfallItem::updatePaintNode;
};

}  // namespace

int main(int argc, char* argv[]) {
  QGuiApplication application(argc, argv);
  TestableSpectrumWaterfallItem item;
  item.setWidth(960.0);
  item.setHeight(540.0);

  QSGNode* node = item.updatePaintNode(nullptr, nullptr);
  if (node == nullptr) return 1;

  node = item.updatePaintNode(node, nullptr);
  delete node;
  return 0;
}
