#include <QCoreApplication>
#include <QNetworkReply>

#include "updates/update_checker.hpp"

int main(int argc, char* argv[]) {
  QCoreApplication application(argc, argv);
  using cwassistant::desktop::update_detail::isTransientFailure;
  using cwassistant::desktop::update_detail::retryDelayMs;

  if (!isTransientFailure(QNetworkReply::ContentNotFoundError, 404) ||
      !isTransientFailure(QNetworkReply::InternalServerError, 500) ||
      !isTransientFailure(QNetworkReply::TimeoutError, 0) ||
      isTransientFailure(QNetworkReply::AuthenticationRequiredError, 401) ||
      isTransientFailure(QNetworkReply::ProtocolInvalidOperationError, 400)) {
    return 1;
  }
  if (retryDelayMs(1) != 400 || retryDelayMs(2) != 1'200 ||
      retryDelayMs(3) != 2'500) {
    return 2;
  }
  return 0;
}
