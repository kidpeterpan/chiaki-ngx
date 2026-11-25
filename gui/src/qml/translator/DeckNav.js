// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// D-pad focus movement for the translator panel. QmlController turns the Steam Deck's
// D-pad into arrow keys; each Deck* control asks this helper for its neighbour instead of
// using Qt's tab chain, which leaves buttons and combo boxes out on some platforms.
.pragma library

function scopeOf(item) {
    for (let node = item ? item.parent : null; node; node = node.parent) {
        if (node.deckNavScope === true)
            return node
    }
    return null
}

function collect(node, out) {
    const children = node.children
    for (let index = 0; index < children.length; ++index) {
        const child = children[index]
        if (!child.visible || !child.enabled)
            continue
        if (child.deckNavigable === true)
            out.push(child)
        else
            collect(child, out)
    }
}

// Visible, enabled Deck* controls in the order they appear on screen.
function navigableItems(scope) {
    const items = []
    collect(scope, items)
    return items
}

// Moves focus to the previous or next control. Returns false at either end.
function move(item, forward) {
    const scope = scopeOf(item)
    if (!scope)
        return false
    const items = navigableItems(scope)
    const target = items[items.indexOf(item) + (forward ? 1 : -1)]
    if (!target)
        return false
    scope.focusControl(target)
    return true
}

// Moves focus to the row above or below: the nearest control in that direction, and
// within that row the one closest horizontally. Down from a tab enters the section rather
// than stepping to the next tab. Returns false when there is no row in that direction.
function moveVertical(item, down) {
    const scope = scopeOf(item)
    if (!scope)
        return false
    const bounds = function(candidate) {
        const origin = candidate.mapToItem(scope, 0, 0)
        return { top: origin.y, bottom: origin.y + candidate.height, centerX: origin.x + candidate.width / 2 }
    }
    const current = bounds(item)
    let rowTop = 0
    let rowBottom = 0
    let best = null
    let bestDistance = 0
    for (const candidate of navigableItems(scope)) {
        if (candidate === item)
            continue
        const box = bounds(candidate)
        const middle = (box.top + box.bottom) / 2
        if (down ? middle <= current.bottom : middle >= current.top)
            continue
        const sameRow = best !== null && middle >= rowTop && middle <= rowBottom
        const closerRow = best === null || (down ? box.bottom < rowTop : box.top > rowBottom)
        const distance = Math.abs(box.centerX - current.centerX)
        if (closerRow) {
            rowTop = box.top
            rowBottom = box.bottom
            best = candidate
            bestDistance = distance
        } else if (sameRow && distance < bestDistance) {
            best = candidate
            bestDistance = distance
        }
    }
    if (!best)
        return false
    // A row can mark the control to land on (the selected tab), as tab bars do on TVs.
    for (const candidate of navigableItems(scope)) {
        const box = bounds(candidate)
        const middle = (box.top + box.bottom) / 2
        if (candidate.deckPreferred === true && middle >= rowTop && middle <= rowBottom) {
            best = candidate
            break
        }
    }
    scope.focusControl(best)
    return true
}

function switchSection(item, step) {
    const scope = scopeOf(item)
    if (scope && scope.cycleSection)
        scope.cycleSection(step)
}
