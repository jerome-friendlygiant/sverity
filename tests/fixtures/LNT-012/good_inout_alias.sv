// sverity-args: --top top
// Reduced from OpenTitan's analog test pad: an inout passed down the hierarchy is one net, not a loop.
// expect-none: LNT-012
module leaf(inout wire pad);
endmodule
module mid(inout wire pad);
  leaf u(.pad(pad));
endmodule
module top(inout wire pad);
  mid u(.pad(pad));
endmodule
