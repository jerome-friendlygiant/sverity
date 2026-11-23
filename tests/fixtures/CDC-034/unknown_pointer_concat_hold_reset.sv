// sverity-args: --top top --goal cdc --sdc @DIR@/../_common/async.sdc
// expect-unresolved: CDC-034
// expect-none: CDC-034 INFO
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, clkb, rst_n, we, re, output logic full, empty);
  logic [3:0] wb, wg, wg_s1, wg_s2, rb, rg, rg_s1, rg_s2;
  always_ff @(posedge clka or negedge rst_n) begin
    if (!rst_n) begin wb <= '0; wg <= '0; end
    else begin
      if (we && !full) wb <= wb + 4'd1;
      wg <= wb ^ (wb >> 1);
    end
  end
  always_ff @(posedge clka or negedge rst_n) begin
    if (!rst_n) begin rg_s1 <= '0; rg_s2 <= '0; end
    else begin rg_s1 <= rg; rg_s2 <= rg_s1; end
  end
  always_ff @(posedge clkb or negedge rst_n) begin
    if (!rst_n) begin
      rb <= '0;
      rg <= {rg[3], 3'b0};
    end else begin
      if (re && !empty) rb <= rb + 4'd1;
      rg <= rb ^ (rb >> 1);
    end
  end
  always_ff @(posedge clkb or negedge rst_n) begin
    if (!rst_n) begin wg_s1 <= '0; wg_s2 <= '0; end
    else begin wg_s1 <= wg; wg_s2 <= wg_s1; end
  end
  assign full = (wg == {~rg_s2[3:2], rg_s2[1:0]});
  assign empty = (rg == wg_s2);
endmodule
