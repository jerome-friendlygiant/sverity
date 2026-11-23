// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Multi-driver always_comb next-state logic without feedback remains Unresolved rather than Disproven.
// expect: CDC-039 WARNING
// expect-unresolved: CDC-039
// expect-none: CDC-028 ERROR
// expect-coverage: payload-qualification INCOMPLETE
module top(input logic clka, clkb, rst_n, load, input logic [3:0] d, local_in, output logic [3:0] q);
  logic [3:0] data, local_q, data_d;
  logic v_a, v1, v2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin data <= '0; v_a <= 1'b0; end
    else begin data <= d; v_a <= load; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin {v2, v1} <= '0; local_q <= '0; end
    else begin {v2, v1} <= {v1, v_a}; local_q <= local_in; end
  always_comb begin
    data_d = q;
    if (v2) data_d = data;
  end
  always_comb begin
    data_d = local_q;
  end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) q <= '0; else q <= data_d;
endmodule
