-- Pandoc filter shared by every Quill export. It turns Quill's prose
-- conventions into each output format:
--
--   ***, ---, * * *     scene break
--   \pagebreak          page break
--   ->text<-            centered line
--
-- and, for PDF and Word, adds the title block and normalizes headings so the
-- top level present in the story becomes chapter level.
--
-- Settings arrive through the environment (QUILL_STYLE, QUILL_TITLE,
-- QUILL_AUTHOR, QUILL_CONTACT, QUILL_WORDS) so multi-line values survive.

local style = os.getenv("QUILL_STYLE") or "manuscript"
local title = os.getenv("QUILL_TITLE") or ""
local author = os.getenv("QUILL_AUTHOR") or ""
local contact = os.getenv("QUILL_CONTACT") or ""
local words = os.getenv("QUILL_WORDS") or ""

local is_docx = FORMAT == "docx"
local is_typst = FORMAT == "typst"
local is_plain = FORMAT == "plain"

local function custom(style_name, blocks)
  return pandoc.Div(blocks, pandoc.Attr("", {}, { ["custom-style"] = style_name }))
end

local function typst_string(text)
  return '"' .. text:gsub("\\", "\\\\"):gsub('"', '\\"'):gsub("\n", "\\n") .. '"'
end

local function scene_break()
  if is_typst then
    return pandoc.RawBlock("typst", "#scenebreak()")
  elseif is_docx then
    return custom("Scene Break", { pandoc.Para({ pandoc.Str(style == "book" and "*   *   *" or "#") }) })
  end
  return pandoc.Para({ pandoc.Str("* * *") })
end

local function page_break()
  if is_typst then
    return pandoc.RawBlock("typst", "#pagebreak()")
  elseif is_docx then
    return pandoc.RawBlock("openxml", '<w:p><w:r><w:br w:type="page"/></w:r></w:p>')
  end
  return {}
end

-- ->text<- markers, possibly split across Str elements by emphasis.
local function strip_centering(inlines)
  local first, last = inlines[1], inlines[#inlines]
  if not first or first.t ~= "Str" or first.text:sub(1, 2) ~= "->" then return nil end
  if not last or last.t ~= "Str" or last.text:sub(-2) ~= "<-" then return nil end
  local content = pandoc.List()
  for _, inline in ipairs(inlines) do content:insert(inline) end
  if #content == 1 then
    local text = first.text:sub(3, -3)
    if text == "" then return nil end
    content[1] = pandoc.Str(text)
    return content
  end
  content[1] = pandoc.Str(first.text:sub(3))
  content[#content] = pandoc.Str(last.text:sub(1, -3))
  if content[#content].text == "" then content:remove(#content) end
  if content[1].text == "" then content:remove(1) end
  while content[1] and content[1].t == "Space" do content:remove(1) end
  while content[#content] and content[#content].t == "Space" do content:remove(#content) end
  return content
end

local function centered(inlines)
  if is_typst then
    local blocks = pandoc.List({ pandoc.RawBlock("typst", "#block(width: 100%, align(center)[") })
    blocks:insert(pandoc.Para(inlines))
    blocks:insert(pandoc.RawBlock("typst", "])"))
    return blocks
  elseif is_docx then
    return { custom("Centered", { pandoc.Para(inlines) }) }
  end
  return { pandoc.Para(inlines) }
end

local function title_block()
  if title == "" or not (is_docx or is_typst) then return pandoc.List() end
  local blocks = pandoc.List()
  if is_typst then
    blocks:insert(pandoc.RawBlock("typst", string.format(
      "#titleblock(title: %s, author: %s, contact: %s, words: %s)",
      typst_string(title), typst_string(author), typst_string(contact), typst_string(words))))
    return blocks
  end

  local function lines_to_para(text)
    local inlines = pandoc.List()
    for line in (text .. "\n"):gmatch("(.-)\n") do
      if #inlines > 0 then inlines:insert(pandoc.LineBreak()) end
      inlines:insert(pandoc.Str(line))
    end
    return pandoc.Para(inlines)
  end

  if style == "manuscript" then
    local first_line = author ~= "" and author or " "
    local rest = contact
    local header = pandoc.List({ pandoc.Str(first_line) })
    if words ~= "" then
      header:insert(pandoc.RawInline("openxml", "<w:r><w:tab/></w:r>"))
      header:insert(pandoc.Str("about " .. words .. " words"))
    end
    local contact_blocks = pandoc.List({ pandoc.Para(header) })
    if rest ~= "" then contact_blocks:insert(lines_to_para(rest)) end
    blocks:insert(custom("Contact", contact_blocks))
    blocks:insert(custom("Story Title", { pandoc.Para({ pandoc.Str(title) }) }))
    if author ~= "" then
      blocks:insert(custom("Byline", { pandoc.Para({ pandoc.Str("by " .. author) }) }))
    end
  else
    blocks:insert(custom("Story Title", { pandoc.Para({ pandoc.Str(title) }) }))
    if author ~= "" then
      blocks:insert(custom("Byline", { pandoc.Para({ pandoc.Str(author) }) }))
    end
    blocks:insert(page_break())
  end
  return blocks
end

local function is_page_break(block)
  return block.t == "RawBlock" and (block.format == "tex" or block.format == "latex")
    and (block.text:match("^%s*\\pagebreak%s*$") or block.text:match("^%s*\\newpage%s*$"))
end

local function is_page_break_para(block)
  if block.t ~= "Para" and block.t ~= "Plain" then return false end
  if #block.content ~= 1 then return false end
  local only = block.content[1]
  return only.t == "RawInline" and (only.format == "tex" or only.format == "latex")
    and (only.text == "\\pagebreak" or only.text == "\\newpage")
end

function Pandoc(doc)
  -- Shift headings so the shallowest level used becomes level one.
  local shallowest = 6
  doc.blocks:walk({ Header = function(h) shallowest = math.min(shallowest, h.level) end })

  local out = title_block()
  -- A book whose text opens with a chapter heading already starts a new
  -- page there; drop the title page's own break to avoid a blank page.
  local first = doc.blocks[1]
  if is_docx and style == "book" and #out > 0 and first and first.t == "Header"
      and first.level == shallowest then
    out:remove(#out)
  end
  local after_break = false
  for _, block in ipairs(doc.blocks) do
    if block.t == "HorizontalRule" then
      out:insert(scene_break())
      after_break = true
    elseif is_page_break(block) or is_page_break_para(block) then
      local replacement = page_break()
      if replacement.t then out:insert(replacement) end
      after_break = true
    elseif block.t == "Para" and strip_centering(block.content) then
      out:extend(centered(strip_centering(block.content)))
      after_break = true
    else
      if block.t == "Header" then
        block.level = block.level - shallowest + 1
        after_break = false
      elseif block.t == "Para" and after_break and is_docx and style == "book" then
        -- Book convention: no first-line indent after a break. (Manuscripts
        -- indent every paragraph.)
        block = custom("First Paragraph", { block })
        after_break = false
      else
        after_break = false
      end
      out:insert(block)
    end
  end
  doc.blocks = out
  return doc
end

-- Plain text drops the emphasis markers pandoc would otherwise keep.
if is_plain then
  function Emph(el) return el.content end
  function Strong(el) return el.content end
  function Strikeout(el) return el.content end
  function Underline(el) return el.content end
end
