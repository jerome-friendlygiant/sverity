// sverity-args: --top top
// expect-unresolved: LNT-057 5
module top #(parameter W = 8) (input logic [1:0] n, output logic [3:0] q);
  logic [W-1:0] mem [2][2];
  assign q = mem[n[0]][n[1]];
endmodule
