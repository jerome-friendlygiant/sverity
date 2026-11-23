// sverity-args: --top top --sdc @DIR@/boundary_reset.sdc --config @DIR@/boundary_reset_long.json
// Required delivery needs an operating contract for sampling outside reset.
// expect: CDC-013 WARNING
// expect-unresolved: CDC-013
module top(input logic src_clk, dst_clk, rst_n, event_i, output logic seen);
  logic s1, s2;
  always_ff @(posedge dst_clk or negedge rst_n)
    if (!rst_n) begin s1 <= 1'b0; s2 <= 1'b0; seen <= 1'b0; end
    else begin s1 <= event_i; s2 <= s1; seen <= seen | s2; end
endmodule
