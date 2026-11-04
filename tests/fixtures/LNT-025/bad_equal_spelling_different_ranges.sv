// sverity-args: --top top
// Equal RHS spelling is insufficient: the low slice writes different bits of the value.
// expect: LNT-025 WARNING
module top(input logic a, b, output logic [3:0] q);
  always_comb begin
    q = 0;
    if (a) q[3:2] = 2'b01;
    if (b) q = 4'b0001;
  end
endmodule
// revert: q[3:2] = 2'b01; => q = 4'b0001;
// revert-expect-none: LNT-025 WARNING
