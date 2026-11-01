// sverity-args: --top top
// the comparison s==1 / s==2 is not exclusive when s is changed inside the first branch
// expect: LNT-025 WARNING
module top(input logic [1:0] sel, input logic a, b, output logic y);
  logic [1:0] s;
  always_comb begin
    s = sel;
    y = 1'b0;
    if (s == 1) begin s = 2; y = a; end
    if (s == 2) y = b;
  end
endmodule
