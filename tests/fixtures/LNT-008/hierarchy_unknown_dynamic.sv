// sverity-args: --top top
// expect: LNT-008 WARNING 4
module top(input logic [1:0] i, input logic a, output logic [3:0] y);
  logic [3:0] d;
  always_comb begin
    d[1:0] = 2'b00;
    d[i] = a;
  end
  assign y = d;
endmodule
