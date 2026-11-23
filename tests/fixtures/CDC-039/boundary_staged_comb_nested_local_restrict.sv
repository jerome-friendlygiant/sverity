// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Nested local restriction inside staged always_comb preserves qualifier necessity.
// expect: CDC-039 WARNING
// expect-confidence: CDC-039 heuristic
// expect-none: CDC-028
// expect-none: CDC-016
module top(input logic clka, clkb, rst_n, load, local_ready, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] data, data_d;
  logic v_a, v1, v2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin data <= '0; v_a <= 1'b0; end
    else begin data <= d; v_a <= load; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) {v2, v1} <= '0; else {v2, v1} <= {v1, v_a};
  always_comb begin
    data_d = q;
    if (v2) begin
      if (local_ready) data_d = data;
    end
  end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) q <= '0; else q <= data_d;
endmodule
