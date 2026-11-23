// sverity-args: --top top --disable-rule CDC-028 --sdc @FIXTURES@/_common/three_async.sdc
// Complementary mux arm reading an asynchronous register from a third clock domain disproves qualification.
// expect: CDC-016 ERROR
// expect-confidence: CDC-016 established
// expect-none: CDC-039
module top(input logic clka, clkb, clkc, rst_n, load, input logic [3:0] da, dc, output logic [3:0] q);
  logic [3:0] data_a, data_c;
  logic v_a, v1, v2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin data_a <= '0; v_a <= 1'b0; end
    else begin data_a <= da; v_a <= load; end
  always_ff @(posedge clkc or negedge rst_n)
    if (!rst_n) data_c <= '0; else data_c <= dc;
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) {v2, v1} <= '0; else {v2, v1} <= {v1, v_a};
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) q <= '0; else q <= v2 ? data_a : data_c;
endmodule
