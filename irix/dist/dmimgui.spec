product dmimgui
    id "Discord Messenger for IRIX (Dear ImGui, OpenGL)"
    image sw
        id "Discord Messenger (ImGui) Software"
        version VERSION
        order 9999
        subsys base default
            id "Discord Messenger with a Dear ImGui interface on OpenGL"
            replaces self
            exp DMIMGUI_BASE
            prereq (
                eoe.sw.base 1289434520 maxint
                eoe.sw.gfx 1289434520 maxint
                x_eoe.sw.eoe 1289434520 maxint
            )
        endsubsys
    endimage
endproduct
