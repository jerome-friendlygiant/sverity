// 32 x 8 memory with two write ports. Both ports are handled in one clocked
// process; when both target the same entry port B takes precedence.
module top (
  input  logic       clk,
  input  logic       we_a,
  input  logic [4:0] addr_a,
  input  logic [7:0] wd_a,
  input  logic       we_b,
  input  logic [4:0] addr_b,
  input  logic [7:0] wd_b,
  input  logic [4:0] raddr,
  output logic [7:0] rdata
);
  logic [7:0] mem [0:31];

  always_ff @(posedge clk) begin
    if (we_a) mem[addr_a] <= wd_a;
    if (we_b) mem[addr_b] <= wd_b;
  end

  assign rdata = mem[raddr];
endmodule
