// sverity-args: --top top
// A constant pattern, literal or named: what `==?` is for.
// expect-none: LNT-040
module top(input logic [7:0] d, output logic m, n);
  localparam logic [7:0] PAT = 8'b1010_????;
  assign m = (d ==? 8'b1010_????);
  assign n = (d !=? PAT);
endmodule
