`timescale 1ns/1ps
module bench;
  logic wr_clk=0, rd_clk=0;
  logic [3:0] wr_gray=0, wr_seen;
  logic [3:0] b=0;
  c09 dut(.*);
  always #4 wr_clk=~wr_clk;
  always #5 rd_clk=~rd_clk;
  function automatic [3:0] gray(input [3:0] x); gray=x^(x>>1); endfunction
  integer i;
  logic [3:0] prev_gray=0;
  initial begin
    for (i=1;i<16;i=i+1) begin
      @(negedge wr_clk); b=i[3:0]; wr_gray=gray(b);
      if ((wr_gray ^ prev_gray) != 4'b0001 && (wr_gray ^ prev_gray) != 4'b0010 && (wr_gray ^ prev_gray) != 4'b0100 && (wr_gray ^ prev_gray) != 4'b1000) $fatal(1,"Gray step changed other than one bit: %b -> %b",prev_gray,wr_gray);
      prev_gray=wr_gray;
    end
    repeat (5) @(posedge rd_clk);
    $display("C09 Gray transition schedule passed; final synchronized value %h",wr_seen); $finish;
  end
  initial begin #1000; $fatal(1,"timeout"); end
endmodule
