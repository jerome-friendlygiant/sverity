// Two independent sticky flags kept in separate processes, read as a 2-bit vector.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       set_ovf,
  input  logic       set_unf,
  input  logic       clear,
  output logic [1:0] flags
);
  logic [1:0] flags_q;

  always @(posedge clk or negedge rst_n) begin
    if (!rst_n)        flags_q[0] <= 1'b0;
    else if (clear)    flags_q[0] <= 1'b0;
    else if (set_ovf)  flags_q[0] <= 1'b1;
  end

  always @(posedge clk or negedge rst_n) begin
    if (!rst_n)        flags_q[1] <= 1'b0;
    else if (clear)    flags_q[1] <= 1'b0;
    else if (set_unf)  flags_q[1] <= 1'b1;
  end

  assign flags = flags_q;
endmodule
