// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Overwriting staged next-state arms with the same data aliases under a different guard must not drop the bypass.
// expect: CDC-028 ERROR
// expect-confidence: CDC-028 established
// expect-none: CDC-039
module top(input logic clka, clkb, rst_n, load, local_en, local_sel, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] data, data_d, raw_alias, q_alias;
  logic v_a, v1, v2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin data <= '0; v_a <= 1'b0; end
    else begin data <= d; v_a <= load; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) {v2, v1} <= '0; else {v2, v1} <= {v1, v_a};
  always_comb begin
    raw_alias = data;
    q_alias = q;
    if (v2) data_d = raw_alias;
    else data_d = q_alias;
    if (local_en) begin
      if (local_sel) data_d = raw_alias;
      else data_d = q_alias;
    end
  end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) q <= '0; else q <= data_d;
endmodule
