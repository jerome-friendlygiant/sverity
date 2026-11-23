// sverity-args: --top top --sdc @DIR@/packed_contract_bit_order.sdc
// expect-count: CST-012 ERROR 1
// expect-count: CST-012 INFO 1
// expect-none: CST-012 WARNING
// Each packed bit has its own output-delay capture contract. q[0] is async to
// clk_b; q[1] is launched by clk_a. The sibling bit cannot change q[0]'s result.
module top(input logic clk_a, clk_b, output logic [1:0] q);
  logic payload_a_q, local_a_q;
  always_ff @(posedge clk_a) payload_a_q <= ~payload_a_q;
  always_ff @(posedge clk_a) local_a_q <= ~local_a_q;
  assign q = {local_a_q, payload_a_q};
endmodule
