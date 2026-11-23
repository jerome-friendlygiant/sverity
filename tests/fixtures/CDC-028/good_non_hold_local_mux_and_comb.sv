// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Safe non-hold local mux and staged always_comb with local default qualify cleanly under CDC-039.
// expect: CDC-039 WARNING
// expect-confidence: CDC-039 heuristic
// expect-none: CDC-028
module top(input logic clka, clkb, rst_n, load, input logic [3:0] d, local_in, output logic [3:0] q, r);
  logic [3:0] data, local_q, r_d;
  logic v_a, v1, v2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin data <= '0; v_a <= 1'b0; end
    else begin data <= d; v_a <= load; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin {v2, v1} <= '0; local_q <= '0; end
    else begin {v2, v1} <= {v1, v_a}; local_q <= local_in; end
  always_comb begin
    r_d = local_q;
    if (v2) r_d = data;
  end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin q <= '0; r <= '0; end
    else begin
      q <= v2 ? data : local_q;
      r <= r_d;
    end
endmodule
