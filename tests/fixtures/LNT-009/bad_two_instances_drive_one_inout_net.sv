// sverity-args: --top top
// Two instances of a child drive one net through an inout port with a plain assignment (no z): slang adds no driver across the inout port.
// expect: LNT-009 ERROR 5
module drv(inout wire p, input logic o);
  assign p = o;
endmodule
module top(inout wire io, input logic o1, o2);
  drv u1(.p(io), .o(o1));
  drv u2(.p(io), .o(o2));
endmodule
