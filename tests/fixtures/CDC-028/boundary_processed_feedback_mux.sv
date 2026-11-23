// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Processed destination-domain feedback in the non-qualifying mux arm qualifies cleanly under CDC-039.
// expect: CDC-039 WARNING
// expect-confidence: CDC-039 heuristic
// expect-none: CDC-028
module top(input logic clka, clkb, rst_n, load, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] data;
  logic v_a, v1, v2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin data <= '0; v_a <= 1'b0; end
    else begin data <= d; v_a <= load; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) {v2, v1} <= '0; else {v2, v1} <= {v1, v_a};
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) q <= '0;
    else q <= v2 ? data : (q + 4'd1);
endmodule
