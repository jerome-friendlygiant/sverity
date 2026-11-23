// sverity-args: --top top --sdc @DIR@/input_async.sdc
// One asynchronous input feeds two independent destination synchronisers.
// revert: {b2, b1} <= {b1, async_in}; => {b2, b1} <= {b1, a2};
// revert-expect-none: CDC-047
// expect: CDC-047 WARNING
module top(input logic clka, clkb, rst_n, async_in, output logic x, y);
  logic a1, a2, b1, b2;
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) {a2, a1} <= '0; else {a2, a1} <= {a1, async_in};
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) {b2, b1} <= '0; else {b2, b1} <= {b1, async_in};
  assign x = a2;
  assign y = b2;
endmodule
