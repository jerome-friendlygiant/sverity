// sverity-args: --top top
// Two pad instances can release a shared inout net (`oe ? o : 1'bz`), and their enables are separate ports: not provably exclusive, so Unresolved.
// expect: LNT-009 WARNING 5
module pad(inout wire p, input logic oe, o);
  assign p = oe ? o : 1'bz;
endmodule
module top(inout wire io, input logic oe1, oe2, o1, o2);
  pad u1(.p(io), .oe(oe1), .o(o1));
  pad u2(.p(io), .oe(oe2), .o(o2));
endmodule
