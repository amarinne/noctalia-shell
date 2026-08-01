#pragma once

#include "ui/controls/flex.h"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

class DragDropController;
class InputArea;

class DragSource : public Flex {
public:
  using DropHandler = std::function<void(std::string payload, std::string target, float sceneX, float sceneY)>;

  explicit DragSource(DragDropController* controller);
  ~DragSource() override;

  DragSource(const DragSource&) = delete;
  DragSource& operator=(const DragSource&) = delete;

  void setDragType(std::string value);
  void setPayload(std::string value);
  void setEnabled(bool enabled);
  void setTooltip(std::string_view text);
  void setOnClick(std::function<void()> callback);
  void setDropHandler(DropHandler handler) { m_dropHandler = std::move(handler); }
  void setSourceOpacity(float opacity);
  void setPreviewAncestor(std::size_t levels);
  void setLiftFromLayout(bool enabled);
  void setDragging(bool dragging);
  void setSize(float width, float height) override;

  [[nodiscard]] std::string_view dragType() const noexcept { return m_dragType; }
  [[nodiscard]] std::string_view payload() const noexcept { return m_payload; }
  [[nodiscard]] bool enabled() const noexcept { return m_enabled; }
  [[nodiscard]] bool dragging() const noexcept { return m_dragging; }
  [[nodiscard]] bool liftFromLayout() const noexcept { return m_liftFromLayout; }
  [[nodiscard]] InputArea* inputArea() const noexcept { return m_inputArea; }
  [[nodiscard]] DragDropController* controller() const noexcept { return m_controller; }
  [[nodiscard]] const DropHandler& dropHandler() const noexcept { return m_dropHandler; }
  [[nodiscard]] Node* previewTarget() noexcept;
  void detachController(DragDropController* controller) noexcept;

protected:
  void doLayout(Renderer& renderer) override;

private:
  void applyVisualState();
  void applyTooltip();
  void updateInputArea();

  DragDropController* m_controller = nullptr;
  InputArea* m_inputArea = nullptr;
  std::string m_dragType;
  std::string m_payload;
  std::string m_tooltip;
  std::function<void()> m_onClick;
  DropHandler m_dropHandler;
  float m_sourceOpacity = 1.0f;
  std::size_t m_previewAncestor = 0;
  bool m_enabled = true;
  bool m_dragging = false;
  bool m_liftFromLayout = false;
};
