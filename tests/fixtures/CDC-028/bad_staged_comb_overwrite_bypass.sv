// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Overwriting data_d after if (v2) with raw cross-domain data under a local enable bypasses qualification.
// expect: CDC-028 ERROR
// expect-confidence: CDC-028 established
// expect-none: CDC-039
module top(input logic clka, clkb, rst_n, load, en_b, input logic [3:0] d1, d2, output logic [3:0] q);
  logic [3:0] data, other_data, data_d;
  logic v_a, v1, v2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin data <= '0; other_data <= '0; v_a <= 1'b0; end
    else begin data <= d1; other_data <= d2; v_a <= load; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) {v2, v1} <= '0; else {v2, v1} <= {v1, v_a};
  always_comb begin
    data_d = q;
    if (v2) data_d = data;
    if (en_b) data_d = other_data;
  end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) q <= '0; else q <= data_d;
endmodule
