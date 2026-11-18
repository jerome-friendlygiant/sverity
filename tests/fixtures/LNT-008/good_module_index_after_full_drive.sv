// sverity-args: --top top
// expect-none: LNT-008
module top(input logic [7:0] a, input logic [2:0] sel, output logic [7:0] y);
  logic [7:0] v;
  always_comb begin
    v = a;
    v[sel] = 1'b0;
    y = v;
  end
endmodule
