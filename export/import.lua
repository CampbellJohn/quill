-- Pandoc filter for opening Word/ODT/RTF files in Quill: keep the prose and
-- its emphasis, drop layout Markdown can't express.

-- Old manuscript convention: underline means italics.
function Underline(el) return pandoc.Emph(el.content) end
function SmallCaps(el) return el.content end
function Span(el) return el.content end
function Div(el) return el.content end
function Image(el) return {} end

-- Scene breaks typed as a line of symbols ("*", "* * *", "#", "~").
local function is_scene_break(block)
  if block.t ~= "Para" and block.t ~= "Plain" then return false end
  local text = pandoc.utils.stringify(block)
  return text:match("^[%s%*#~•⁂]+$") ~= nil and text:match("[%*#~•⁂]") ~= nil
end

function Blocks(blocks)
  local out = pandoc.List()
  for _, block in ipairs(blocks) do
    if block.t == "HorizontalRule" or is_scene_break(block) then
      out:insert(pandoc.RawBlock("markdown", "* * *"))
    else
      out:insert(block)
    end
  end
  return out
end

-- Pandoc lifts a Word "Title" paragraph into metadata; put it back as the
-- story's heading so it isn't lost.
function Pandoc(doc)
  if doc.meta.title then
    doc.blocks:insert(1, pandoc.Header(1, pandoc.utils.stringify(doc.meta.title)))
    doc.meta.title = nil
  end
  return doc
end
