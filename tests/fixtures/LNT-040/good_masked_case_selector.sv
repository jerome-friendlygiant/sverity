// sverity-args: --top top
// expect-none: LNT-040
module top(input logic data, output logic y);
  localparam logic [2:0] OPTIONS = 3'bzx1;
  always_comb case (OPTIONS & 3'b001)
    1: y = data;
    default: y = 0;
  endcase
endmodule
