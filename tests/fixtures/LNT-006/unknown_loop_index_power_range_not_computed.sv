// sverity-args: --top top --option LNT-006.dynamic_index=true
// Only loop counters and constants decide the index, but the model has no range for a power: the tool gave up, which is
// Unresolved without any input contract behind it.
// expect: LNT-006 WARNING 10
// expect-unresolved: LNT-006 10
module top(input logic [7:0] d, output logic [7:0] q);
  always_comb begin
    q = '0;
    for (int i = 0; i < 3; i++)
      q[i ** 2] = d[i];
  end
endmodule
