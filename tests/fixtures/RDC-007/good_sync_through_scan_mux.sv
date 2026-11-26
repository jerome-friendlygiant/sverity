// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Reset synchronisers in the receiving domain are recognised by shape regardless of consumer (scan mux or valid flag).
// expect-none: RDC-007
module top(input logic clka, clkb, rst_n, req, scanmode, scan_rst_n, d, output logic q, v);
  logic rg;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) rg <= 1'b0; else rg <= req;
  logic s1, s2, rst_o;
  always_ff @(posedge clkb or negedge rg) if (!rg) begin s1 <= 1'b0; s2 <= 1'b0; end else begin s1 <= 1'b1; s2 <= s1; end
  assign rst_o = scanmode ? scan_rst_n : s2;
  always_ff @(posedge clkb or negedge rst_o) if (!rst_o) q <= 1'b0; else q <= d;
  logic v1, v2;
  always_ff @(posedge clkb or negedge rg) if (!rg) begin v1 <= 1'b0; v2 <= 1'b0; end else begin v1 <= 1'b1; v2 <= v1; end
  assign v = v2;
endmodule
