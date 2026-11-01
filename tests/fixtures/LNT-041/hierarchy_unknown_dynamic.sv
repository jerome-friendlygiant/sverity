// sverity-args: --top top
// expect: LNT-041 WARNING 4
module top(input logic [1:0] i, input logic a, output logic y);
  logic [1:0] m [2];
  always_comb begin
    m[0] = 2'b00;
    m[i] = {a, a};
  end
  assign y = m[0][0];
endmodule
