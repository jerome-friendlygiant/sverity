// sverity-args: --top top
// An X in a range bound of an inside set is a relational comparison, not a wildcard.
// expect: LNT-040 WARNING 6
module top(input logic [3:0] a, output logic y);
  localparam logic [3:0] PX = 4'b00x0;
  assign y = a inside {[PX : 4'd9]};
endmodule
