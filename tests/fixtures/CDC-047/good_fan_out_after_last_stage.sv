// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Near miss of bad_two_parallel_synchronisers: one chain, fan-out only after its last stage.
// expect-none: CDC-047
module top(input logic clka, clkb, rst_n, d, output logic x, y);
  logic launch, a1, a2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) launch <= 0; else launch <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {a2, a1} <= '0; else {a2, a1} <= {a1, launch};
  assign x = a2;
  assign y = a2;
endmodule
