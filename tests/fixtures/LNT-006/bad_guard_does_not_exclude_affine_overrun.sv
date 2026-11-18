// sverity-args: --top top --option LNT-006.dynamic_index=true
// `i < 7 || en` still lets `i = 7` through, so `a[i+1]` may reach 8: a guard is honoured only where it excludes the iteration.
// expect: LNT-006 WARNING 8
module top(input logic [7:0] d, input logic en, output logic [7:0] y);
  logic [7:0] a;
  always_comb begin
    a = '0;
    for (int i = 0; i < 8; i++) if (i < 7 || en) a[i+1] = d[i];
    y = a;
  end
endmodule
