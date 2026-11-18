// sverity-args: --top top
// g = '0 runs whatever the loop does and covers g[7], which the other process also writes.
// expect: LNT-009 ERROR 10
module top(input logic [7:0] req, input logic c, output logic [7:0] y);
  logic [7:0] g;
  always @* begin
    g = '0;
    for (int i = 0; i < 8; i++) begin if (req[i]) begin g[i] = 1'b1; break; end end
  end
  always @(c) g[7] = c;
  assign y = g;
endmodule
