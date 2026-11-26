// Counter block whose reset combines a board reset with a power-on reset.
module top (
    input  logic       clk,
    input  logic       pad_rst_n,
    input  logic       por_n,
    input  logic       en,
    output logic [3:0] count
);
    logic core_rst_n;

    assign core_rst_n = pad_rst_n & por_n;

    always_ff @(posedge clk or negedge core_rst_n) begin
        if (!core_rst_n) count <= '0;
        else if (en)     count <= count + 1'b1;
    end
endmodule
