// sverity-args: --top top --option LNT-006.dynamic_index=true
// The write runs past the range only on activations where `en` is set; the default check cannot show `en` is ever
// set, so it stays silent, and the opt-in reports the loop's overrun as a heuristic WARNING (the guard is not read).
// expect: LNT-006 WARNING 9
module top(input logic en, input logic [7:0] d, output logic [3:0] q);
  always_comb begin
    q = '0;
    for (int i = 0; i < 8; i++)
      if (en) q[i] = d[i];
  end
endmodule
