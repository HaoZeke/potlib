`scripts/export_uma_aoti.py` no longer stops with ".numpy() is not supported
for tensor subclasses" after a successful static nonstrict export under
torch 2.8, where the exported module returns tensor subclasses.
