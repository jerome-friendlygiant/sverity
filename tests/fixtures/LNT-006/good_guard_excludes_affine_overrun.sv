// sverity-args: --top top --option LNT-006.dynamic_index=true
// `if (i < 7)` keeps `i+1` inside [7:0]: the condition narrows the loop variable, so the opt-in finds nothing.
// expect-none: LNT-006
module top(input logic [7:0] d, output logic [7:0] y);
  logic [7:0] a;
  always_comb begin
    a = '0;
    for (int i = 0; i < 8; i++) if (i < 7) a[i+1] = d[i];
    y = a;
  end
endmodule
