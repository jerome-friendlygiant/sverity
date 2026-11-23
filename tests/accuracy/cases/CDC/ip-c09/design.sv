// C09
module c09(input logic wr_clk, rd_clk, input logic [3:0] wr_gray, output logic [3:0] wr_seen);
  (* ASYNC_REG = "TRUE" *) logic [3:0] g1;
  (* ASYNC_REG = "TRUE" *) logic [3:0] g2;
  always_ff @(posedge rd_clk) begin g1<=wr_gray; g2<=g1; end
  assign wr_seen=g2;
endmodule
