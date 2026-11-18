// sverity-args: --top top --option LNT-006.dynamic_index=true
// The loop runs to 9, but the remainder keeps the read index in 0..7: its range is computed, not the whole int range.
// expect-none: LNT-006
module top(input logic [7:0] d, output logic [7:0] q);
  always_comb begin
    q = '0;
    for (int i = 0; i < 10; i++) q[i % 8] = d[i % 8];
  end
endmodule
