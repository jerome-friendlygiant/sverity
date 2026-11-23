// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Complementary raw cross-domain arms in always_comb remain an established CDC-028 hazard.
// expect: CDC-028 ERROR
// expect-confidence: CDC-028 established
// expect-none: CDC-039
module top(input logic clka, clkb, rst_n, load, input logic [3:0] d1, d2, output logic [3:0] q);
  logic [3:0] data_a, data_b, data_d;
  logic v_a, v1, v2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin data_a <= '0; data_b <= '0; v_a <= 1'b0; end
    else begin data_a <= d1; data_b <= d2; v_a <= load; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) {v2, v1} <= '0; else {v2, v1} <= {v1, v_a};
  always_comb begin
    if (v2) data_d = data_a;
    else data_d = data_b;
  end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) q <= '0; else q <= data_d;
endmodule
