// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Combinational next-state expansion exceeding the depth limit without feedback stays Unresolved.
// expect: CDC-039 WARNING
// expect-unresolved: CDC-039
// expect-none: CDC-028 ERROR
// expect-coverage: payload-qualification INCOMPLETE
module top(input logic clka, clkb, rst_n, load, input logic [3:0] d, local_in, output logic [3:0] q);
  logic [3:0] data, local_q, h0, h1, h2, h3, h4, h5, h6, h7, h8;
  logic v_a, v1, v2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin data <= '0; v_a <= 1'b0; end
    else begin data <= d; v_a <= load; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin {v2, v1} <= '0; local_q <= '0; end
    else begin {v2, v1} <= {v1, v_a}; local_q <= local_in; end
  always_comb begin
    h0 = local_q;
    if (v2) h0 = data;
  end
  assign h1 = h0;
  assign h2 = h1;
  assign h3 = h2;
  assign h4 = h3;
  assign h5 = h4;
  assign h6 = h5;
  assign h7 = h6;
  assign h8 = h7;
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) q <= '0; else q <= h8;
endmodule
