// Address decoder child and a write-enabled control register behind it.
module addr_match #(
  parameter logic [15:0] BASE      = 16'h4000,
  parameter int          LOG2_SIZE = 8
) (
  input  logic [15:0] addr,
  output logic        hit
);
  assign hit = (addr[15:LOG2_SIZE] == BASE[15:LOG2_SIZE]);
endmodule

module top (
  input  logic        clk,
  input  logic        rst_n,
  input  logic [15:0] addr,
  input  logic        wr,
  input  logic [7:0]  wdata,
  output logic [7:0]  ctrl_q
);
  addr_match #(.BASE(16'h4000), .LOG2_SIZE(8)) u_dec (
    .addr (addr),
    .hit  (hit_w)
  );

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)            ctrl_q <= 8'h00;
    else if (wr && hit_w)  ctrl_q <= wdata;
  end
endmodule
