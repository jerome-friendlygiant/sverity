// sverity-args: --top top
// A black-box IOBUF on io may drive it, and the parent also drives io: what the box drives is not modelled, so Unresolved.
// expect: LNT-009 WARNING 8
(* black_box *) module IOBUF(inout wire IO, input logic I, T, output logic O);
endmodule
module top(inout wire io, input logic o, oe, output logic i);
  IOBUF u(.IO(io), .I(o), .T(~oe), .O(i));
  assign io = 1'b0;
endmodule
