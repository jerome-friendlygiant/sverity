// sverity-args: --top top --disable-rule CDC-028 --sdc @FIXTURES@/_common/async.sdc
// Non-qualifying mux arm reading another raw cross-domain launch register remains an established CDC-016 hazard.
// expect: CDC-016 ERROR
// expect-confidence: CDC-016 established
// expect-none: CDC-039
module top(input logic clka, clkb, rst_n, load, input logic [3:0] d1, d2, output logic [3:0] q);
  logic [3:0] data, alt_data;
  logic v_a, v1, v2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin data <= '0; alt_data <= '0; v_a <= 1'b0; end
    else begin data <= d1; alt_data <= d2; v_a <= load; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) {v2, v1} <= '0; else {v2, v1} <= {v1, v_a};
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) q <= '0;
    else q <= v2 ? data : alt_data;
endmodule
