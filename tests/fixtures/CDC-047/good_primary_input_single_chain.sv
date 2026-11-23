// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A single input chain fans out only after its final stage.
// expect-none: CDC-047
module top(input logic clka, clkb, rst_n, async_in, output logic x, y);
  logic a1, a2;
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) {a2, a1} <= '0; else {a2, a1} <= {a1, async_in};
  assign x = a2;
  assign y = a2;
endmodule
